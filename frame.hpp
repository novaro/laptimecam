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

#ifndef FRAME__HPP
#define FRAME__HPP

/*
 * Data representation and buffering.
 */

#include <inttypes.h>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <list>
#include <chrono>
#include "types.hpp"

#define NB_V4L2_BUFFER 10 
#define NB_MY_BUFFER 20

template <class T>
class BufferQueue
{
	public:
		class Interrupt {};

		BufferQueue ( void ) :
			m_mutex ()
			, m_cond ()
			, m_queue ()
			, m_interrupted ( false ) {}

		void push ( T item )
		{
			std::unique_lock<std::mutex> lck ( m_mutex );
			m_queue.push_back( std::move( item ) );
			m_cond.notify_all();
		}

		/**
		 * Will block if the queue is empty.
		 */
		T pop ( void ) throw ( Interrupt )
		{
			std::unique_lock<std::mutex> lck ( m_mutex );
			while ( !m_interrupted && m_queue.empty() ) m_cond.wait( lck );

			if ( m_interrupted )
			{
				m_interrupted = false;
				throw Interrupt ();
			}

			T item = std::move( m_queue.front() );
			m_queue.pop_front();
			return item;
		}

		/**
		 * Can be used to unblock when ending a thread.
		 */
		void interrupt ( void )
		{
			std::unique_lock<std::mutex> lck ( m_mutex );
			m_interrupted = true;
			m_cond.notify_all();
		}

	private:
		std::mutex m_mutex;
		std::condition_variable m_cond;
		std::list<T> m_queue;
		bool m_interrupted;
};

class Frame
{
	public:
		typedef enum
		{
			FMT_YUYV /* raw data from V4L2 */
			, FMT_Y  /* only 8bit Y component */
		} format_t;

		Frame ( int width, int height, format_t fmt, void* pool );
		~Frame ( void );
		const int m_size;
		uint8_t* m_data;
		const int m_width;
		const int m_height;
		const format_t m_format;

		my_clock::time_point m_timestamp;
		int m_sincePrevMs;

		/**
		 * Get luminance component.
		 */
		uint8_t getY( int x, int y );

		bool isCompatible ( const Frame& other );

	public:
		/// FOR INTERNAL USE ONLY!
		static void __release ( Frame* self );
	private:
		/**
		 * Pointer to parent pool for releasing a frame. ( though I don't
		 * necessarily like pointers being littered around like this... )
		 */
		void* m_pool;
};

typedef std::shared_ptr<Frame> frameref;
typedef BufferQueue<frameref> frame_queue;

#endif
