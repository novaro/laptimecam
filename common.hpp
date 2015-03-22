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

#ifndef COMMON__HPP
#define COMMON__HPP

#include <thread>
#include <functional>
#include <iostream>
#include <sstream>

#define IMAGE_WIDTH  320
#define IMAGE_HEIGHT 240

/**
 * Add thread stop control to std::thread.
 */
class ThreadControl
{
	public:
		template <class Fn, class Host, class... Args>
		ThreadControl ( Fn&& fn, Host&& host, Args&&... args ) :
			m_run ( true )
			, m_thread ( std::bind( fn, host, this, args... ) )
	{
	}

		~ThreadControl ( void ) {}

		void stop ( void ) { m_run = false; }
		void join ( void ) { m_thread.join(); }
		bool isRunning ( void ) { return m_run; }
	private:
		bool m_run;
		std::thread m_thread;
};

extern bool debug;

class Debug
{
	public:
		Debug ( const char* id, int line ) : out () {
			if ( debug )
			{
				out << "[" << id << ":" << line << "] ";
			}
		}
		/* pass a function which will be executed if debug is enabled */
		Debug ( const char* id, int line, std::function<void ( std::stringstream& )> fn ) : out () {
			if ( debug )
			{
				out << "[" << id << ":" << line << "] ";
				fn( out );
			}
		}

		~Debug ( void )
		{
			if ( debug )
			{
				std::cout << out.str();
			}
		}
		std::stringstream out;
};
#define DBGFUN(...) Debug( __FILE__, __LINE__, __VA_ARGS__ )
#define DBGOUT Debug( __FILE__, __LINE__ ).out

#endif
