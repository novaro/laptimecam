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

import sys
import subprocess
import os
import cfg

config = cfg.Config()
name = '[' + os.path.basename( __file__ ) + ']'

# at least for now an external utility 'uvcdynctrl' is used to set fixed focus
try:
    subprocess.check_call( [ 'uvcdynctrl', '-s', 'Focus, Auto', '0' ] )
    subprocess.check_call( [ 'uvcdynctrl', '-s', 'Focus (absolute)', config.getString( 'focus', 'video' ) ] )
except OSError:
    print name + " Setting fixed focus failed, external utility 'uvcdynctrl' not installed?"
except subprocess.CalledProcessError:
    print name + " Setting fixed focus using 'uvcdynctrl' failed."

executable = os.path.join( cfg.BASEDIR, 'capture' )
args = [
    executable
    , '-d', config.getString( 'device', 'video' )
    , '-o', config.getString( 'capture_root', 'dirs' )
    # TODO: allocate ports dynamically
    , '-l', config.getString( 'zmq1', 'ipc' )
    , '-r', config.getString( 'zmq2', 'ipc' )
    , '-p', cfg.HTTP_PATH_CAPTURE + '/'
    , '--trigger', config.getString( 'trigger', 'runtime' )
    , '--ignore', config.getString( 'ignore_limit', 'video' )
    , '--maximages', config.getString( 'image_limit', 'video' )
    ]
# pass optional arguments to 'capture'
args.extend( sys.argv[1:] )

os.execv( executable, args )

