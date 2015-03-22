/* 
 * laptimecam
 * Copyright (C) 2015 Mikko Novaro
 *
 * This file includes a section derived from uvccapture, which was released
 * under "GPLv2 or later". uvccapture copyright holders:
 *
 * Orginally Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard
 * Modifications Copyright (C) 2006  Gabriel A. Devenyi
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

#include "capture.hpp"

#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>

#include <algorithm>

Camera::Camera ( const std::string& video_device, int width, int height ) :
	m_width ( width )
	, m_height ( height )
	, m_format ( V4L2_PIX_FMT_YUYV )
	, m_prevFrame ( my_clock::now() )
{
	init_v4l2( video_device );

	/*
	 * Populate frame buffer only after the frame size has been
	 * established
	 */

	for ( int i = 0 ; i < NB_MY_BUFFER ; ++i )
	{
		Frame* f = new Frame( width, height, Frame::FMT_YUYV, &m_framePool );
		m_framePool.push( frameref( f, Frame::__release ) );
	}
}

void Camera::capture ( void )
{
	/* stream on */
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret = ioctl( m_fd, VIDIOC_STREAMON, &type );
	if ( ret < 0 )
	{
		perror( "VIDIOC_STREAMON" );
		throw 0;
	}
}

/* Origin: uvccapture/v4l2uvc.c */
int Camera::init_v4l2 ( const std::string& video_device )
{
	int i;
	int ret = 0;
	int debug = 0; // mno

	if ((m_fd = open ( video_device.c_str(), O_RDWR)) == -1) {
		perror ("ERROR opening V4L interface \n");
		exit (1);
	}
	memset (&m_cap, 0, sizeof (struct v4l2_capability));
	ret = ioctl (m_fd, VIDIOC_QUERYCAP, &m_cap);
	if (ret < 0) {
		fprintf ( stderr, "Error opening device %s: unable to query device.\n",
				video_device.c_str() );
		goto fatal;
	}

	if ((m_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
		fprintf (stderr,
				"Error opening device %s: video capture not supported.\n",
				video_device.c_str() );
		goto fatal;;
	}
	//			if (vd->grabmethod) {
	if (!(m_cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf (stderr, "%s does not support streaming i/o\n",
				video_device.c_str() );
		goto fatal;
	}
	//			} else {
	//				if (!(m_cap.capabilities & V4L2_CAP_READWRITE)) {
	//					fprintf (stderr, "%s does not support read i/o\n", VIDEO_DEVICE);
	//					goto fatal;
	//				}
	//			}
	/* set format in */
	memset (&m_fmt, 0, sizeof (struct v4l2_format));
	m_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	m_fmt.fmt.pix.width = m_width;
	m_fmt.fmt.pix.height = m_height;
	m_fmt.fmt.pix.pixelformat = m_format;
	m_fmt.fmt.pix.field = V4L2_FIELD_ANY;
	ret = ioctl (m_fd, VIDIOC_S_FMT, &m_fmt);
	if (ret < 0) {
		fprintf (stderr, "Unable to set format: %d.\n", errno);
		goto fatal;
	}
	if ((m_fmt.fmt.pix.width != m_width) ||
			(m_fmt.fmt.pix.height != m_height)) {
		fprintf (stderr, " format asked unavailable get width %d height %d \n",
				m_fmt.fmt.pix.width, m_fmt.fmt.pix.height);
		m_width = m_fmt.fmt.pix.width;
		m_height = m_fmt.fmt.pix.height;
		/* look the format is not part of the deal ??? */
		//vd->formatIn = m_fmt.fmt.pix.pixelformat;
	}
	/* request buffers */
	memset (&m_rb, 0, sizeof (struct v4l2_requestbuffers));
	m_rb.count = NB_V4L2_BUFFER;
	m_rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	m_rb.memory = V4L2_MEMORY_MMAP;

	ret = ioctl (m_fd, VIDIOC_REQBUFS, &m_rb);
	if (ret < 0) {
		fprintf (stderr, "Unable to allocate buffers: %d.\n", errno);
		goto fatal;
	}
	/* map the buffers */
	for (i = 0; i < NB_V4L2_BUFFER; i++) {
		memset (&m_buffer, 0, sizeof (struct v4l2_buffer));
		m_buffer.index = i;
		m_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		m_buffer.memory = V4L2_MEMORY_MMAP;
		ret = ioctl (m_fd, VIDIOC_QUERYBUF, &m_buffer);
		if (ret < 0) {
			fprintf (stderr, "Unable to query buffer (%d).\n", errno);
			goto fatal;
		}
		if (debug)
			fprintf (stderr, "length: %u offset: %u\n", m_buffer.length,
					m_buffer.m.offset);
		m_mem[i] = mmap (0 /* start anywhere */ ,
				m_buffer.length, PROT_READ, MAP_SHARED, m_fd,
				m_buffer.m.offset);
		if (m_mem[i] == MAP_FAILED) {
			fprintf (stderr, "Unable to map buffer (%d)\n", errno);
			goto fatal;
		}
		if (debug)
			fprintf (stderr, "Buffer mapped at address %p.\n", m_mem[i]);
	}
	/* Queue the buffers. */
	for ( i = 0; i < NB_V4L2_BUFFER; ++i )
	{
		memset (&m_buffer, 0, sizeof (struct v4l2_buffer));
		m_buffer.index = i;
		m_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		m_buffer.memory = V4L2_MEMORY_MMAP;
		ret = ioctl (m_fd, VIDIOC_QBUF, &m_buffer);
		if (ret < 0) {
			fprintf (stderr, "Unable to queue buffer (%d).\n", errno);
			goto fatal;;
		}
	}
	return 0;
fatal:
	return -1;
}

void Camera::grab ( frame_queue* fq )
{
	int ret = -1;
	memset( &m_buffer, 0, sizeof ( struct v4l2_buffer ) );
	m_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	m_buffer.memory = V4L2_MEMORY_MMAP;
	ret = ioctl( m_fd, VIDIOC_DQBUF, &m_buffer );
	if ( ret < 0 )
	{
		perror( "VIDIOC_DQBUF" );
		throw 0;
	}

	my_clock::time_point now = my_clock::now();

	/* process data */

	switch ( m_format )
	{
		case V4L2_PIX_FMT_YUYV:
			{
				using namespace std::chrono;
				duration<double> diff = duration_cast<duration<double>>( now - m_prevFrame );
				m_prevFrame = now;

				frameref f = m_framePool.pop();
				memcpy( f->m_data, m_mem[ m_buffer.index ], m_buffer.bytesused );
				f->m_timestamp = now;
				f->m_sincePrevMs = duration_cast<milliseconds>( diff ).count();
				fq->push( std::move( f ) );
			}
			break;
		case V4L2_PIX_FMT_MJPEG:
		default:
			throw 0;
			break;
	}

	/* release */

	ret = ioctl( m_fd, VIDIOC_QBUF, &m_buffer );
	if ( ret < 0 )
	{
		perror( "VIDIOC_QBUF" );
		throw 0;
	}
}

void Camera::grabFrames ( ThreadControl* tc, frame_queue* fq )
{
	while ( tc->isRunning() )
	{
		grab( fq );
	}
}

