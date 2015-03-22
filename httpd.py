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

import cherrypy
import glob
import os
import sys
from cherrypy.lib.static import serve_file
from cherrypy.process import plugins
import zmq
import unicodedata
import jinja2
import re
import urllib
import json
import string
import shutil
import threading
import thread

import cfg

################################################################################

config = cfg.Config()

################################################################################

# zmq

def enum ( **enums ):
    return type( 'Enum', (), enums )
Modes = enum( IDLE = 'mode,idle', PREVIEW = 'mode,preview', CAPTURE = 'mode,capture' )

class CamDaemon:
    def __init__( self ):
        self.context = zmq.Context()
        self.socket = self.context.socket( zmq.REQ )
        self.socket.connect( config.getString( 'zmq1', 'ipc' ) )
        self.socket.RCVTIMEO = 1000 #ms
        self.lock = thread.allocate_lock()
    def sendreq( self, msg ):
        self.lock.acquire()
        try:
            self.socket.send_string( msg )
            return self.socket.recv_string()
        finally:
            self.lock.release()
    def sendreqRaw( self, msg ):
        self.lock.acquire()
        try:
            self.socket.send_string( msg )
            return self.socket.recv()
        finally:
            self.lock.release()
    def setMode( self, mode ):
        try:
            return self.sendreq( mode )
        except:
            pass

cam = CamDaemon()

################################################################################

class Session:
    def __init__ ( self, capture_root, session_root, session_name ):
        self.captureRoot = capture_root
        self.sessionRoot = session_root
        self.sessionName = session_name
        self.jsonFullPath = os.path.join( self.sessionRoot, self.sessionName, 'session.json' )
        f = open( self.jsonFullPath, 'r' )
        self.data = json.load( f )
        f.close()

    def save ( self ):
        f = open( self.jsonFullPath, 'w' )
        json.dump( self.data, f )
        f.close

    def selectFrames ( self, jsonIds ):
        # TODO: unselect others
        #self.data['images'][:] = [ img['kind'] = '0'
        for i in jsonIds:
            self.selectFrame( i )

    def selectFrame ( self, selector ):
        # validate selector
        if sorted( selector.keys() ) != ['id', 'kind']:
            raise cherrypy.HTTPError( 400 )
        if not selector['kind'] in ['1', '2', '3']:
            raise cherrypy.HTTPError( 400 )

        for i, img in enumerate( self.data['frames'] ):
            if img['id'] == selector['id']:
                publicPath = os.path.join( 'sessions', self.sessionName, img['id'] + '.jpg' )
                if img['url'] != publicPath:
                    shutil.move(
                            os.path.join( self.captureRoot, os.path.basename( img['url'] ) ) # TODO: proper url->filesystem de-mapping, or don't use path in capture "url"
                            , os.path.join( self.sessionRoot, self.sessionName, img['id'] + '.jpg' )
                            )
                    img['url'] = publicPath
                img['kind'] = selector['kind']
                self.data['frames'][i] = img # replace
                break

    def truncate( self ):
        ''' remove image references from the data '''
        self.data.pop( 'frames', None )

    def removeCapturedImages ( self ):
        ''' remove image references and files from the filesystem '''
        for path in [ img['url'] for img in self.data['frames'] if img['kind'] == '0' ]:
            os.remove( os.path.join( self.captureRoot , os.path.basename( path ) ) ) # TODO: proper url->filesystem de-mapping, or don't use path in capture "url"
        # retain references with kind != '0'
        self.data['frames'][:] = [ img for img in self.data['frames'] if img['kind'] != '0' ]

    def deleteFromDisk ( self ):
        shutil.rmtree( os.path.join( self.sessionRoot, self.sessionName ) );

################################################################################

class CaptureJsonSink ( threading.Thread ):
    def __init__ ( self ):
        threading.Thread.__init__( self )
        self.context = zmq.Context()
        self.socket = self.context.socket( zmq.PULL )
        self.socket.connect( config.getString( 'zmq2', 'ipc' ) )
        self.socket.RCVTIMEO = 3000 #ms
        self.running = True
        self.outputlock = thread.allocate_lock()
        self.output = None

    def run ( self ):
        while self.running:
            try:
                msg = self.socket.recv_string()
                self.outputlock.acquire()
                try:
                    if not self.output is None:
                        self.output.write( msg + "," )
                finally:
                    self.outputlock.release()
            except:
                pass

    def stop ( self ):
        self.running = False

    def setOutput ( self, outfile ):
        self.outputlock.acquire()
        try:
            self.output = outfile
        finally:
            self.outputlock.release()

capsink = CaptureJsonSink()

################################################################################

class CaptureControl:
    def __init__ ( self, name ):
        name = validate_session_name( name )
        sessiondir = config.getString( 'session_root', 'dirs' )
        extension = ''
        i = 0
        while os.path.exists( os.path.join( sessiondir , name + extension ) ):
            i += 1
            extension = "-" + str( i )
        self.name = name + extension
        os.makedirs( os.path.join( sessiondir , self.name ) )
        self.f = open( os.path.join( sessiondir , self.name , "session.json" ), "w" )
        self.f.write( '{ "lap_length": "' + config.getString( 'lap_length' ) + '",\n' )
        self.f.write( '"frames":[ ' ) # the trailing space is intentional
        capsink.setOutput( self.f )
        cam.setMode( Modes.CAPTURE )

    def __del__ ( self ):
        cam.setMode( Modes.IDLE )
        capsink.setOutput( None )
        # erase last comma produced by CaptureJsonSink ( or the cautionary
        # space in case there weren't any captures )
        self.f.seek( -1, 1 )
        self.f.write( ']}' )

################################################################################

class SinkHandler ( plugins.SimplePlugin ):
    def start( self ):
        capsink.start()

    def stop( self ):
        capsink.stop()

################################################################################

# jinja
jinjaenv = jinja2.Environment( loader = jinja2.FileSystemLoader(
            os.path.join( cfg.BASEDIR, 'templates' ) ) )
# FIXME: 2014-12-24-wheezy-raspbian required adding urlencode in jinjaenv.filters

################################################################################

def validate_session_name( val ):
    # allowed characters for a session name (erase others)
    return re.sub( '[^A-Za-z0-9_:-]+', '', val )

################################################################################

class Root( object ):

    def __init__ ( self ):
        self.capcontrol = None
        self.sessiondir = config.getString( 'session_root', 'dirs' )

    @cherrypy.expose
    @cherrypy.tools.json_out()
    def mode( self, name ):
        if name == 'preview':
            mode = Modes.PREVIEW
        elif name == 'capture':
            mode = Modes.CAPTURE
        else:
            mode = Modes.IDLE
        return { 'reply' : cam.setMode( mode ) }

    @cherrypy.expose
    @cherrypy.tools.json_out()
    def preview_json( self ):
        return { 'trigger' : cam.sendreq( "preview,trigger" ) }

    @cherrypy.expose
    def preview_jpg( self, **args ):
        msg = cam.sendreqRaw( "preview,jpg" )
        cherrypy.response.headers['Cache-Control'] = 'no-cache'
        cherrypy.response.headers['Pragma-directive'] = 'no-cache'
        cherrypy.response.headers['Cache-directive'] = 'no-cache'
        cherrypy.response.headers['Cache-control'] = 'no-cache'
        cherrypy.response.headers['Pragma'] = 'no-cache'
        cherrypy.response.headers['Expires'] = '0'
        if ( len( msg ) < 10 ):
            return serve_file( os.path.join( cfg.BASEDIR, 'html/blank.png' ) ) # FIXME
        else:
            cherrypy.response.headers['Content-Type'] = 'image/jpg'
            return msg

    @cherrypy.expose
    def style_css( self, page ):
        if page in [ 'Setup', 'Capture', 'Sessions', 'Session' ]:
            cherrypy.response.headers['Content-Type'] = 'text/css'
            templ = jinjaenv.get_template( page + '.css' )
            return templ.render()
        else:
            raise cherrypy.HTTPError( 400 )

    @cherrypy.expose
    def sessions_css( self ):
        cherrypy.response.headers['Content-Type'] = 'text/css'
        templ = jinjaenv.get_template( 'sessions.css' )
        return templ.render()

    @cherrypy.expose
    @cherrypy.tools.json_out()
    def config_json( self, key, **args ):
        # some unnecessary duplication here, clean up possibly...
        def validate_set_float( key, val, val_min, val_max ):
            try:
                valFloat = float( val )
                if ( valFloat < val_min or valFloat > val_max ):
                    raise ValueError
                config.setStr( key, str( valFloat ) )
            except ValueError:
                val = str( config.getFloat( key ) )
            return val

        def validate_set_int( key, val, val_min, val_max ):
            try:
                valInt = int( val )
                if ( valInt < val_min or valInt > val_max ):
                    raise ValueError
                config.setStr( key, str( valInt ) )
            except ValueError:
                val = str( config.getInt( key ) )
            return val

        def validate_set_string( key, val ):
            val = validate_session_name( val )
            try:
                orig = config.getString( key )
            except:
                orig = ''
            if val != orig:
                config.setStr( key, str( val ) )
            return val

        ########## SET
        if 'val' in args:
            val = args[ 'val' ]

            # validate allowed configuration keys
            if key == 'trigger': # or key == 'focus':
                val = validate_set_float( key, val, 0.0, 1.0 )
                cam.sendreq( "config,trigger," + val )
            if key == 'lap_length':
                val = validate_set_float( key, val, 0.0, sys.float_info.max )
            elif key == 'session_prefix' or key == 'session_suffix':
                val = validate_set_string( key, val )
            #elif key == 'image_limit':
            #    val = validate_set_int( key, val, 0, 10000 )
        ########## GET
        else:
            try:
                val = config.getString( key )
            except:
                val = ''
        
        return { 'val': val }

    @cherrypy.expose
    @cherrypy.tools.json_out()
    def validate_session_name_json( self, val ):
        return { 'val' : validate_session_name( val ) }

    # start session capture
    @cherrypy.expose
    @cherrypy.tools.json_out()
    def capture_json ( self, name ):
        if not self.capcontrol is None:
            del self.capcontrol
            self.capcontrol = None
        self.capcontrol = CaptureControl( name )
        return { "name" : self.capcontrol.name }

    #################### single session info json
    @cherrypy.expose
    @cherrypy.tools.json_out()
    def session_json ( self, name, **args ):
        try:
            name = validate_session_name( name )
            s = Session( config.getString( 'capture_root', 'dirs' ), config.getString( 'session_root', 'dirs' ), name )
            if 'postselect' in args.keys():
                jsonIds = json.loads( args['postselect'] );
                s.selectFrames( jsonIds )
                s.removeCapturedImages()
                s.save()
                return {}
            # TODO: fill in missing values (laps, total time etc.)
            # remove image refs for 'brief'
            if 'brief' in args.keys():
                s.truncate()
            if 'delete' in args.keys():
                try:
                    s.removeCapturedImages()
                except:
                    pass
                s.deleteFromDisk()
                return {}
            return s.data
        except:
            raise
        return {}

    # setup page
    def setup( self ):
        templ = jinjaenv.get_template( 'setup.html' )
        return templ.render()

    # capture page
    def capture( self ):
        try:
            prefix = config.getString( 'session_prefix' )
        except:
            prefix = ''
        try:
            suffix = config.getString( 'session_suffix' )
        except:
            suffix = ''
        templ = jinjaenv.get_template( 'capture.html' )
        return templ.render( sessionNamePrefix = prefix, sessionNameSuffix = suffix );

    # sessions listing page
    def sessions( self ):
        templ = jinjaenv.get_template( 'sessions.html' )
        sessions = sorted( os.walk( self.sessiondir ).next()[1] , reverse=True)
        return templ.render( sessions = sessions )

    # single session page
    def session( self, **args ):
        if not 'name' in args.keys():
            raise cherrypy.HTTPError( 500 )
        if not os.path.isfile( os.path.join( self.sessiondir , args['name'] , 'session.json' ) ):
            raise cherrypy.HTTPError( 404 )

        templ = jinjaenv.get_template( 'single.html' )
        return templ.render( sessionId = args['name'] )

    @cherrypy.expose
    def index( self, page = '', **args ):

        # terminate capture
        if not self.capcontrol is None:
            del self.capcontrol
            self.capcontrol = None
        else:
            cam.setMode( Modes.IDLE ) # always reset to idle at page load

        if page == 'capture':
            return self.capture()
        elif page == 'sessions':
            return self.sessions()
        elif page == 'session':
            return self.session( **args )
        else:
            return self.setup()
        return templ.render()

deps = [ 'html/import/jquery.js', 'html/import/jquery.tablesorter.js', 'html/import/jss.js' ]
for d in deps:
    if not os.path.isfile( d ):
        print "Static html dependency missing. Check 'get_html_deps.sh' for automatic downloading."
        sys.exit( 1 )

sinkhand = SinkHandler( cherrypy.engine )
sinkhand.subscribe()
cherrypy.quickstart( Root(), '/', config.getCherryPyConfig() )

