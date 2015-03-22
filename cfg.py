#!/usr/bin/env python
# -*- coding: utf-8 -*-
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

################################################################################
#
# laptimecam
# Copyright (C) 2015 Mikko Novaro
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
################################################################################

import ConfigParser
import thread
import os
import sys

################################################################################

CONFIGFILENAME='~/.laptimecamrc'
HTTP_PATH_CAPTURE='capture'
BASEDIR=os.path.dirname( os.path.realpath( __file__ ) )

################################################################################

# Implement thread safe config file get and set (with immediate re-write).
class Config:
    def __init__( self ):
        defaults = {
            'runtime' : {
                'trigger' : '0.03'
                , 'lap_length' : '333.3'
            }
            , 'httpd' : {
                'host' : '0.0.0.0'
                , 'port' : '8080'
            }
            , 'dirs' : {
                'session_root' : '/var/local/laptimecam/sessions'
                , 'capture_root' : '/var/local/laptimecam/capture'
            }
            , 'ipc' : {
                # octet notification works with both bindings (c++ & python)
                  'zmq1' : 'tcp://127.0.0.1:58673'
                , 'zmq2' : 'tcp://127.0.0.1:58674'
            }
            , 'video' : {
                'device' : '/dev/video0'
                , 'focus' : '0'
                , 'ignore_limit' : '10'
                , 'image_limit' : '1000'
            }
        }

        self.filename = os.path.expanduser( CONFIGFILENAME )
        self.section = 'runtime'

        self.config = ConfigParser.RawConfigParser()

        # set default values without using [DEFAULT] section
        for section, values in defaults.iteritems():
            self.config.add_section( section )
            for k, v in values.iteritems():
                self.config.set( section, k, v )

        try:
            self.config.readfp( open( self.filename ) )
        except:
            with open( self.filename, 'wb' ) as configfile:
                self.config.write( configfile )
            print "Default configuration file " + CONFIGFILENAME + " created. Verify values ==> exiting."
            sys.exit( 1 )
        self.lock = thread.allocate_lock()

    def getFloat( self, key ):
        self.lock.acquire()
        try:
            return self.config.getfloat( self.section, key )
        finally:
            self.lock.release()

    def getString( self, key, section = None ):
        if not section:
            section = self.section
        self.lock.acquire()
        try:
            return self.config.get( section, key )
        finally:
            self.lock.release()

    def getInt( self, key ):
        self.lock.acquire()
        try:
            return self.config.getint( self.section, key )
        finally:
            self.lock.release()

    def setStr( self, key, val ):
        self.lock.acquire()
        try:
            self.config.set( self.section, str( key ), str( val ) )
            with open( self.filename, 'wb' ) as configfile:
                self.config.write( configfile )
        finally:
            self.lock.release()

    def getCherryPyConfig( self ):
        # translate for CherryPy initialization
        self.lock.acquire()
        try:
            return {
                'global' : {
                    'server.socket_port' : self.config.getint( 'httpd', 'port' )
                    , 'server.socket_host' : self.config.get( 'httpd', 'host' )
                }
                , '/' : {
                    'tools.staticdir.on' : True,
                    'tools.staticdir.dir' : os.path.join( BASEDIR, 'html' )
                }
                , '/sessions' : {
                    'tools.staticdir.on' : True,
                    'tools.staticdir.dir' : self.config.get( 'dirs', 'session_root' )
                }
                , '/'+HTTP_PATH_CAPTURE : {
                    'tools.staticdir.on' : True,
                    'tools.staticdir.dir' : self.config.get( 'dirs', 'capture_root' )
                }
            }
        finally:
            self.lock.release()

