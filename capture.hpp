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
#ifndef CAPTURE__HPP
#define CAPTURE__HPP

#include <linux/videodev2.h>

#include "frame.hpp"
#include "common.hpp"
#include "types.hpp"

class Camera
{
	public:
		Camera ( const std::string& video_device, int width = 0, int height = 0 );
		void capture ( void );
		/* derived from uvccapture */
		int init_v4l2 ( const std::string& video_device );
		void grab ( frame_queue* fq );
		void grabFrames ( ThreadControl* tc, frame_queue* fq );

	private:
		int m_fd;
		struct v4l2_buffer m_buffer;
		struct v4l2_capability m_cap;
		struct v4l2_format m_fmt;
		struct v4l2_requestbuffers m_rb;
		void* m_mem[NB_V4L2_BUFFER];

		int m_width;
		int m_height;
		int m_format;

		frame_queue m_framePool;

		my_clock::time_point m_prevFrame;
};

#endif
