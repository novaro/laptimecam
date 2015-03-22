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

#include "analyze.hpp"
#include <ctime>
#include <cmath>
#include <algorithm>

#define CHANGE_FUN(__c__) ( log( 1.0 + ( __c__ * 20.0 ) ) / M_E )

MotionDetector::MotionDetector ( double trigger_threshold, uint8_t ignore_threshold, bool update_max, double max_period_sec ) :
	m_triggerThreshold ( trigger_threshold )
	, m_ignoreThreshold ( ignore_threshold )
	, m_updateMax ( update_max )
	, m_maxPeriodSec ( max_period_sec )
	, m_maxChange ( 0.0 )
{
}

void MotionDetector::consumeFrames ( ThreadControl* tc, frame_queue* inq, frame_queue* outq )
{
	frameref prev ( NULL );
	while ( tc->isRunning() )
	{
		try
		{
			frameref f = inq->pop();

			if ( prev )
			{
				Frame* f1 = prev.get();
				Frame* f2 = f.get();
				if ( f1->isCompatible( *f2 ) )
				{
					/* perform comparison */
					uint32_t sum = 0;
					uint32_t max = ( 255 - m_ignoreThreshold ) * f1->m_width * f1->m_height;
					int step = f1->m_format == Frame::FMT_YUYV ? 2 : 1;
					for ( int i = 0 ; i < f1->m_size ; i += step )
					{
						int y1 = f1->m_data[i];
						int y2 = f2->m_data[i];
						int diff = y1 - y2;
						if ( diff < 0 ) diff *= -1;
						if ( diff >= m_ignoreThreshold )
						{
							sum += ( diff - m_ignoreThreshold );
						}
					}
					/* use percentage to have a value independent of image size */
					double change = CHANGE_FUN( static_cast<double>( sum ) / static_cast<double>( max ) );
					/*
					 * determine local max from a backlog
					 */
					if ( m_updateMax )
					{
						using namespace std::chrono;
						m_changeLog.push_back( std::make_pair( f2->m_timestamp, change ) );
						auto it = m_changeLog.begin();
						while (
								it != m_changeLog.end() &&
								duration_cast<duration<double>>( f2->m_timestamp - it->first ).count() > m_maxPeriodSec
							  )
						{
							m_changeLog.erase( it );
							it = m_changeLog.begin();
						}
						double max = 0.0;
						for ( auto it = m_changeLog.begin() ; it != m_changeLog.end() ; ++it )
						{
							if ( it->second > max ) max = it->second;
						}
						m_maxChange = max;
					}

					if ( outq && change >= m_triggerThreshold )
					{
						/* TODO: store an *unwritten* 'prev' as a reference at
						 * trigger */
						outq->push( f );
					}
				}
			}

			prev = std::move( f );
			// frameref will be automatically deallocated/released
		}
		catch ( frame_queue::Interrupt& )
		{
			DBGOUT << __FUNCTION__ << " interrupted" << std::endl;
		}
	}
	DBGOUT << __FUNCTION__ << " terminating" << std::endl;
}

