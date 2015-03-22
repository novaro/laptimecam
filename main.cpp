/* 
 * laptimecam
 * Copyright (C) 2015 Mikko Novaro
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <thread>
#include <zmq.hpp>
#include <functional>
#include <sstream>
#include <getopt.h>

#include "capture.hpp"
#include "analyze.hpp"
#include "output.hpp"

#define DEFAULT_VIDEO_DEVICE "/dev/video0"
#define DEFAULT_TRIGGER_THRESHOLD 0.03
#define DEFAULT_IGNORE_THRESHOLD 10
/**
 * Limits the number of written images per session (fail-safe measure).
 */
#define DEFAULT_IMAGE_LIMIT 1000

#define TRIGGER_MIN 0.0
#define TRIGGER_MAX 1.0
#define IGNORE_MIN 0
#define IGNORE_MAX 255

bool debug = false;

class Mode
{
	public:
		Mode ( const std::string& name ) : m_name ( name ) {}
		virtual ~Mode ( void ) {}
		std::string name ( void ) { return m_name; }
	private:
		std::string m_name;
};

class ModeIdle : public Mode
{
	public:
		ModeIdle ( void ) : Mode ( "idle" ) {}
};

class ModePreview : public Mode
{
	public:
		ModePreview ( Camera* cam, uint8_t ignore_threshold ) :
			Mode ( "preview" )
			, m_analyzeQueue ()
			, m_outputQueue ()
			, m_detector ( 0.0, ignore_threshold, true, 10.0 )
			, m_preview ()
			, m_grabber ( &Camera::grabFrames, cam, &m_analyzeQueue )
			, m_consumer (
					&MotionDetector::consumeFrames, &m_detector, &m_analyzeQueue, &m_outputQueue
					)
			, m_output ( &Preview::outputFrames, &m_preview, &m_outputQueue )
	{
	}

		virtual ~ModePreview ( void )
		{
			m_grabber.stop();
			m_consumer.stop();
			m_output.stop();

			m_analyzeQueue.interrupt();
			m_outputQueue.interrupt();

			m_grabber.join();
			m_consumer.join();
			m_output.join();
		}

		void sendJpegByZmq ( zmq::socket_t& socket )
		{
			unsigned long size = 0;
			uint8_t* jpeg = m_preview.getJpeg( size );
			socket.send( jpeg, size );
		}

	private:
		frame_queue m_analyzeQueue;
		frame_queue m_outputQueue;
	public:
		MotionDetector m_detector;
		Preview m_preview;
	private:
		ThreadControl m_grabber;
		ThreadControl m_consumer;
		ThreadControl m_output;
};

class ModeCapture : public Mode
{
	public:
		ModeCapture (
				Camera* cam
				, const std::string& zmq_remote_addr
				, const std::string& output_dir
				, const std::string& url_prefix
				, double trigger_threshold
				, uint8_t ignore_threshold
				, int image_limit
				) :

			Mode ( "capture" )
			, m_analyzeQueue ()
			, m_writeQueue ()
			, m_detector ( trigger_threshold, ignore_threshold )
			, m_dispatcher ( zmq_remote_addr, output_dir, url_prefix, image_limit )

			, m_grabber ( &Camera::grabFrames, cam, &m_analyzeQueue )
			, m_consumer ( &MotionDetector::consumeFrames, &m_detector, &m_analyzeQueue, &m_writeQueue )
			, m_writer ( &Dispatcher::writeFrames, &m_dispatcher, &m_writeQueue )
			{
			}

		/**
		 * ThreadControl destructors would stop and join the threads,
		 * and frame_queue destructors will 
		 */
		virtual ~ModeCapture ( void )
		{
			/*
			 * ThreadControl destructors could also stop and join the
			 * threads, but they MUST be stopped BEFORE other
			 * resources (such as frame_queues) are torn down to
			 * prevent any race conditions.
			 */
			m_grabber.stop();
			m_consumer.stop();
			m_writer.stop();

			/*
			 * Wake up threads blocking on the frame queues.
			 */
			m_analyzeQueue.interrupt();
			m_writeQueue.interrupt();

			m_grabber.join();
			m_consumer.join();
			m_writer.join();
		}

	private:
		frame_queue m_analyzeQueue;
		frame_queue m_writeQueue;

		MotionDetector m_detector;
		Dispatcher m_dispatcher;

		ThreadControl m_grabber;
		ThreadControl m_consumer;
		ThreadControl m_writer;
};

std::vector<std::string> splitString ( const std::string& s, char delim )
{
	std::vector<std::string> tokens;
	std::string::const_iterator wordStart = s.begin();
	for ( std::string::const_iterator it = s.begin() ; it != s.end() ; ++it )
	{
		if ( *it == delim )
		{
			tokens.push_back( std::string( wordStart, it ) );
			wordStart = it + 1;
		}
	}
	if ( wordStart != s.end() ) tokens.push_back( std::string( wordStart, s.end() ) ); // the rest
	return tokens;
}

class ZmqRep
{
	public:
		ZmqRep ( zmq::socket_t& socket ) : m_socket ( socket ), m_responded ( false ) {}
		~ZmqRep ( void )
		{
			if ( !m_responded )
			{
				m_socket.send( "error", 5 );
			}
		}
		void ok ( void )
		{
			m_socket.send( "ok", 2 );
			m_responded = true;
		}
		void send ( const std::string& msg )
		{
			m_socket.send( msg.c_str(), msg.size() );
			m_responded = true;
		}
		void setResponded ( void )
		{
			m_responded = true;
		}
	private:
		zmq::socket_t& m_socket;
		bool m_responded;
};

void usage ( void )
{
	printf(
			"Image capture daemon, invocate using 'run_capture.py'.\n"
			"Mandatory arguments:\n"
			"\t-o <output directory>   \n"
			"\t-l <local zmq address>  \n"
			"\t-r <remote zmq address> \n"
			"Optional arguments:\n"
			"\t-d <capture device>     default is " DEFAULT_VIDEO_DEVICE "\n"
			"\t-p <url prefix>         map filesystem path to httpd url\n"
			"\t--trigger <0.0-1.0>     trigger limit, default %.2f\n"
			"\t--ignore <0-255>        ignore limit, default %u\n"
			"\t--maximages <-1|limit>  max number of images written per session, default %u, -1=no limit\n"
			"\t--debug                 debug\n"
			"\t--help                  show this help text\n"
			, DEFAULT_TRIGGER_THRESHOLD
			, DEFAULT_IGNORE_THRESHOLD
			, DEFAULT_IMAGE_LIMIT
			);
}

int main ( int argc, char** argv )
{
	/*
	 * Parse command line args
	 */
	std::string zmqLocal = "";
	std::string zmqRemote = "";
	std::string outputDir = "";
	std::string urlPrefix = "";
	std::string videoDevice = DEFAULT_VIDEO_DEVICE;
	double triggerThreshold = DEFAULT_TRIGGER_THRESHOLD;
	uint8_t ignoreThreshold = DEFAULT_IGNORE_THRESHOLD;
	int imageLimit = DEFAULT_IMAGE_LIMIT;

	struct option long_options[] = {
		{ "debug", no_argument, 0, 0 }
		, { "help", no_argument, 0, 0 }
		, { "trigger", required_argument, 0, 0 }
		, { "ignore", required_argument, 0, 0 }
		, { "maximages", required_argument, 0, 0 }
		, { 0, 0, 0, 0 }
	};

	while ( true )
	{
		int option_index = 0;
		int c = getopt_long( argc, argv, "o:l:r:p:d:", long_options, &option_index );
		if (c == -1)
			break;

		try
		{
			switch (c)
			{
				case 0:
					{
						std::string opt = std::string( long_options[ option_index ].name );
						if ( opt == "debug" )
						{
							for ( int i = 0 ; i < argc ; ++i ) printf( "%s ", argv[ i ] );
							printf( "\n" );
							debug = true;
						}
						else if ( opt == "help" )
						{
							usage();
							return( EXIT_SUCCESS );
						}
						else if ( opt == "trigger" )
						{
							triggerThreshold = std::stod( optarg );
							if ( triggerThreshold < TRIGGER_MIN || triggerThreshold > TRIGGER_MAX )
								throw "--trigger out of bounds";
						}
						else if ( opt == "ignore" )
						{
							ignoreThreshold = std::stoi( optarg );
							if ( ignoreThreshold < IGNORE_MIN || ignoreThreshold > IGNORE_MAX )
								throw "--ignore out of bounds";
						}
						else if ( opt == "maximages" )
						{
							imageLimit = std::stoi( optarg );
							if ( imageLimit < -1 ) throw "--maximages out of bounds";
						}
					}
					break;

				case 'd':
					videoDevice = std::string( optarg );
					break;

				case 'l':
					zmqLocal = std::string( optarg );
					break;

				case 'r':
					zmqRemote = std::string( optarg );
					break;

				case 'o':
					outputDir = std::string( optarg );
					break;

				case 'p':
					urlPrefix = std::string( optarg );
					break;

				default:
					throw "";
					break;
			}
		}
		catch ( const char* msg )
		{
			if ( strlen( msg ) > 0 ) fprintf( stderr, "ERROR: %s\n", msg );
			usage();
			return( EXIT_FAILURE );
		}
	}

	if ( optind < argc || zmqLocal.empty() || zmqRemote.empty() || outputDir.empty() )
	{
		usage();
		return( EXIT_FAILURE );
	}

	Camera camera ( videoDevice, IMAGE_WIDTH, IMAGE_HEIGHT );
	camera.capture();

	Mode* mode = new ModeIdle();

	/* zmq */
#define MSG_SIZE ( 1024 )
	zmq::message_t msg ( MSG_SIZE );
	zmq::context_t ctx;
	zmq::socket_t responder ( ctx, ZMQ_REP );
	responder.bind( zmqLocal.c_str() ); // throws zmq::error_t

	while ( true )
	{
		if ( responder.recv( &msg ) )
		{
			ZmqRep reply ( responder );
			std::string reqstr ( reinterpret_cast<char*>( msg.data() ), msg.size() );
			msg.rebuild( MSG_SIZE );

			std::vector<std::string> req = splitString( reqstr, ',' );
			DBGFUN ( [&]( std::stringstream& out ) {
				std::string separator = "";
				out << "ZmqReq: [ ";
				for_each( req.begin(), req.end(), [&]( std::string& s ) {
					out << separator << s;
					separator = " | ";
				} );
				out << " ]" << std::endl;
			} );

			if ( req.size() > 0 )
			{
				/*
				 * Switch mode
				 */
				if ( req[0] == "mode" && req.size() >= 2 )
				{
					DBGOUT << "Mode: " << req[1].c_str() << std::endl;
					if ( req[1] == "idle" )
					{
						delete mode;
						mode = new ModeIdle();
						reply.ok();
					}
					else if ( req[1] == "capture" )
					{
						delete mode;
						mode = new ModeCapture(
								&camera, zmqRemote, outputDir, urlPrefix, triggerThreshold, ignoreThreshold, imageLimit );
						reply.ok();
					}
					else if ( req[1] == "preview" )
					{
						delete mode;
						mode = new ModePreview( &camera, ignoreThreshold );
						reply.ok();
					}
				}
				/*
				 * Send preview jpeg or trigger level info
				 */
				else if ( req[0] == "preview" && mode->name() == "preview" && req.size() == 2 )
				{
					ModePreview* preview = dynamic_cast<ModePreview*>( mode );
					if ( req[1] == "jpg" )
					{
						preview->sendJpegByZmq( responder );
						reply.setResponded();
					}
					else if ( req[1] == "trigger" )
					{
						std::string trigger = std::to_string( preview->m_detector.getMaxChange() );
						reply.send( trigger );
					}
				}
				/*
				 * Runtime configuration
				 */
				else if ( req[0] == "config" && req.size() == 3 )
				{
					if ( req[1] == "trigger" )
					{
						try
						{
							double trigger = std::stod( req[2] );
							if ( trigger >= TRIGGER_MIN && trigger <= TRIGGER_MAX )
							{
								DBGOUT << "Config: setting trigger to " << trigger << std::endl;
								triggerThreshold = trigger;
							}
						}
						catch ( ... )
						{
						}
					}
				}
			}
		}
	}
	return 0;
}

