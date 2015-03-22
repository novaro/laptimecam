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

#ifndef OUTPUT__HPP
#define OUTPUT__HPP

#include <zmq.hpp>
#include <mutex>
#include "frame.hpp"
#include "common.hpp"

class Dispatcher
{
	public:
		Dispatcher (
				const std::string& zmq_publisher_addr
				, const std::string& output_dir
				, const std::string& url_prefix
				, int image_limit
				);
		void writeFrames ( ThreadControl* tc, frame_queue* fq );
	private:
		zmq::context_t m_context;
		zmq::socket_t m_jsonSocket;
		std::string m_outputDir;
		std::string m_urlPrefix;
		const int m_imageLimit;
};

class Preview
{
	public:
		Preview ( void );
		void outputFrames ( ThreadControl* tc, frame_queue* fq );
		uint8_t* getJpeg ( unsigned long& size );

	private:
		std::mutex m_requestMutex;
		std::mutex m_replyMutex;
		std::condition_variable m_replyCond;
		bool m_getJpeg;
		uint8_t* m_jpegData;
		unsigned long m_jpegSize;
};

#endif
