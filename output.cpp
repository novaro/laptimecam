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

#include "output.hpp"
#include <jpeglib.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <cstring>

void compressJpeg (
		Frame* frame
		, FILE* outfile
		, uint8_t** outbuffer
		, unsigned long* outsize
		, int quality )
{
	struct jpeg_compress_struct info;
	struct jpeg_error_mgr err;

	info.err = jpeg_std_error( &err );
	jpeg_create_compress( &info );

	/* output compressed image either into a file or memory buffer */
	if ( outfile )
	{
		jpeg_stdio_dest( &info, outfile );
	}
	else if ( outbuffer )
	{
		jpeg_mem_dest( &info, outbuffer, outsize );
	}

	info.image_width = frame->m_width;
	info.image_height = frame->m_height;
	info.input_components = 3;
	info.in_color_space = JCS_YCbCr; /* use colour components compatible with the captured data */

	jpeg_set_defaults( &info );
	jpeg_set_quality( &info, quality, TRUE );

	jpeg_start_compress( &info, TRUE );

	uint8_t outrow[ IMAGE_WIDTH * 3 ]; // JCS_YCbCr uses 3 bytes / pixel
	uint8_t* inptr = frame->m_data;
	JSAMPROW rowPtr[1] = { outrow };

	while ( info.next_scanline < info.image_height )
	{
		/*
		 * Format used for the webcam images is
		 * http://linuxtv.org/downloads/v4l-dvb-apis/V4L2-PIX-FMT-YUYV.html
		 * 
		 * and libjpeg was configured to take JCS_YCbCr
		 *
		 * ==> map input "Y Cb Y Cr" to output "Y Cb Cr"
		 */

		uint8_t* outptr = &outrow[0];

		for ( int i = 0 ; i < info.image_width / 2 ; ++i ) // two pixels handled at a time
		{
			int y0 = inptr[ 0 ];
			int cb = inptr[ 1 ];
			int y1 = inptr[ 2 ];
			int cr = inptr[ 3 ];
			outptr[ 0 ] = y0;
			outptr[ 3 ] = y1;
			outptr[ 1 ] = outptr[ 4 ] = cb;
			outptr[ 2 ] = outptr[ 5 ] = cr;
			inptr += 4;
			outptr += 6;
		}

		jpeg_write_scanlines( &info, rowPtr, 1 );
	}

	jpeg_finish_compress( &info );
	jpeg_destroy_compress( &info );
}

Dispatcher::Dispatcher (
		const std::string& zmq_publisher_addr
		, const std::string& output_dir
		, const std::string& url_prefix
		, int image_limit
		) :
	m_context ()
	, m_jsonSocket ( m_context, ZMQ_PUSH )
	, m_outputDir ( output_dir )
	, m_urlPrefix ( url_prefix )
	, m_imageLimit ( image_limit )
{
	m_jsonSocket.bind( zmq_publisher_addr.c_str() );
}

void Dispatcher::writeFrames ( ThreadControl* tc, frame_queue* fq )
{
	using namespace std::chrono;
	auto begin = my_clock::now(); // output times starting from the start of a session
	auto prevWritten = my_clock::now();
	int id = 0; // IDs unique per session
	static int img = 0; // filenames unique per execution
	char path[1024];
	while ( tc->isRunning() )
	{
		try
		{
			frameref f = fq->pop(); // empty queue even if image write limit is exceeded

			if ( m_imageLimit < 0 || id < m_imageLimit )
			{
				++id;
				++img;
				if ( m_imageLimit == id ) DBGOUT << "image capture limit reached" << std::endl;

				sprintf( path, "%s/%i.jpg", m_outputDir.c_str(), img ); // FIXME: use something safer, or check bounds
				FILE* outfile = fopen( path, "wb");
				if ( outfile )
				{
					compressJpeg( f.get(), outfile, NULL, NULL, 90 );
					fclose( outfile );
					std::stringstream json;
					json << "{\"id\":\"" << id << "\",";
					json << "\"since_captured\":\"" << f->m_sincePrevMs << "\",";
					json << "\"since_written\":\"" << duration_cast<milliseconds>( f->m_timestamp - prevWritten ).count() << "\",";
					json << "\"kind\":\"0\",";
					json << "\"url\":\"" << m_urlPrefix << img << ".jpg\",";
					json << "\"time\":\"" << duration_cast<milliseconds>( f->m_timestamp - begin ).count() << "\"}";
					std::string str = json.str();
					m_jsonSocket.send( str.c_str(), str.size() );
					prevWritten = f->m_timestamp;
				}
			}
		}
		catch ( frame_queue::Interrupt& )
		{
			DBGOUT << __FUNCTION__ << " interrupted" << std::endl;
		}
	}
	DBGOUT << __FUNCTION__ << " terminating" << std::endl;
}

Preview::Preview ( void ) :
	m_requestMutex ()
	, m_replyMutex ()
	, m_replyCond ()
	, m_getJpeg ( false )
	, m_jpegData ( NULL )
	, m_jpegSize ( 0 )
{
}

void Preview::outputFrames ( ThreadControl* tc, frame_queue* fq )
{
	while ( tc->isRunning() )
	{
		try
		{
			frameref f = fq->pop();

			/* compress to jpeg only if another thread requests it */
			std::unique_lock<std::mutex> reqlck ( m_requestMutex );
			if ( m_getJpeg )
			{
				std::unique_lock<std::mutex> replck ( m_replyMutex );
				uint8_t* outbuffer = NULL;
				unsigned long outsize = 0;
				compressJpeg( f.get(), NULL, &outbuffer, &outsize, 90 );

				m_jpegData = new uint8_t[ outsize ];
				m_jpegSize = outsize;
				memcpy( m_jpegData, outbuffer, outsize );

				free( outbuffer );

				m_getJpeg = false;
				m_replyCond.notify_all();
			}
		}
		catch ( frame_queue::Interrupt& )
		{
		}
	}
}

uint8_t* Preview::getJpeg ( unsigned long& size )
{
	{
		std::unique_lock<std::mutex> lck ( m_requestMutex );
		m_getJpeg = true;
	}
	std::unique_lock<std::mutex> lck ( m_replyMutex );
	while ( !m_jpegData ) m_replyCond.wait( lck );

	size = m_jpegSize;
	uint8_t* data = m_jpegData;

	m_jpegData = NULL;
	m_jpegSize = 0;

	return data;
}

