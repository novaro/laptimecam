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

#include "frame.hpp"

Frame::Frame ( int width, int height, format_t fmt, void* pool ) :
	m_size ( fmt == FMT_YUYV ? 2 * width * height : width * height )
	, m_data ( new uint8_t[ m_size ] )
	, m_width ( width )
	, m_height ( height )
	, m_format ( fmt )
	, m_timestamp ( )
	, m_pool ( pool )
{
}

Frame::~Frame ( void )
{
	if ( m_data ) delete m_data;
}

uint8_t Frame::getY( int x, int y )
{
	if ( x > m_width || y > m_height ) return 0;
	int index = x * ( m_format == FMT_YUYV ? 2 : 1 ) + y * m_width;
	if ( index > m_size ) return 0;
	return m_data[ index ];
}

bool Frame::isCompatible ( const Frame& other )
{
	return
		m_size == other.m_size
		&& m_width == other.m_width
		&& m_format == other.m_format
		;
}

void Frame::__release ( Frame* self )
{
	frame_queue* fq = reinterpret_cast<frame_queue*>( self->m_pool );
	fq->push( frameref( self, &Frame::__release ) ); // reconstruct ptr
}

