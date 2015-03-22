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

function FormatTime ( time_ms )
{
		var totalMs = parseInt( time_ms );
		var hour = Math.floor( totalMs / 1000 / 60 / 60 );
		var min = Math.floor( totalMs / 1000 / 60 ) % ( 60 );
		var sec = Math.floor( totalMs / 1000 ) % ( 60 );
		var sec100th = Math.floor( totalMs / 10 ) % ( 100 );
		/* display hours only after at least an hour has elapsed */
		var prefix = "";
		if ( hour > 0 )
		{
				prefix += hour + ":";
		}
		return prefix + pad( min, 2 ) + ":" + pad( sec, 2 ) + "." + pad( sec100th, 2 )
}

function Speed ( distance_m, time_ms )
{
		var distKm = distance_m / 1000.0;
		var timeH = time_ms / 1000.0 / 60.0 / 60.0;
		return ( distKm / timeH ).toFixed( 2 );
}

function Dist ( distance_m )
{
		return distance_m.toFixed( 0 );
}

//--------------------------------------------------------------------------------

function Lap ( distance, time_start_ms, time_finish_ms )
{
		this.number = -1;
		this.distance = distance;
		this.start = time_start_ms;
		this.finish = time_finish_ms;
		this.time = time_finish_ms - time_start_ms;
}

Lap.prototype.getSpeed = function()
{
		return Speed( this.distance, this.time );
}

Lap.prototype.getTime = function()
{
		return this.finish - this.start;
}

//--------------------------------------------------------------------------------

function Segment ()
{
		this.laps = [];
}

Segment.prototype.addLap = function ( lap )
{
		lap.number = this.laps.length + 1;
		this.laps.push( lap );
}

Segment.prototype.lastLap = function ()
{
		return this.laps[ this.laps.length - 1 ];
}

Segment.prototype.firstLap = function ()
{
		return this.laps[ 0 ];
}

Segment.prototype.lap = function ( lap )
{
		return this.laps[ lap - 1 ];
}

/* Get average speed for the whole segment if lap is not given. Otherwise get
 * average for 'lap' first laps. */
Segment.prototype.getSpeed = function ( lap )
{
		lap = typeof lap !== 'undefined' ? lap : this.laps.length;
		return Speed( this.laps[ 0 ].distance * lap, this.getTime( lap ) );
}

/* Get time for the whole segment if lap is not given. Otherwise get time for
 * 'lap' first laps. */
Segment.prototype.getTime = function ( lap )
{
		lap = typeof lap !== 'undefined' ? lap : this.laps.length;
		if ( this.laps.length == 0 )
				return 0;
		else
				return this.lap( lap ).finish - this.firstLap().start;
}

Segment.prototype.getDistance = function ()
{
		return this.laps[ 0 ].distance * this.laps.length;
}

//--------------------------------------------------------------------------------

Session.frametype =  {
		NONE : -1
		, START : 0
		, LAP : 1
		, SPLIT : 2
		, FINISH : 3
};

function Session ( lap_length )
{
		this.segments = [];
		this.segments.push( new Segment() );
		this.prevFrame = null;
		this.lapLength = lap_length;
}

/* get last / current segment */
Session.prototype.segment = function ( offset )
{
		offset = typeof offset !== 'undefined' ? offset : 0;
		return this.segments[ this.segments.length + offset - 1 ];
}

/*
 * Return: frame identity, Session.frametype
 */
Session.prototype.feedFrame = function ( frameJson )
{
		var type = Session.frametype.NONE;
		if ( frameJson.kind == '0' ) return type;
		if ( this.prevFrame )
		{
				var lap = new Lap( this.lapLength, this.prevFrame.time, frameJson.time );
				this.segment().addLap( lap );
				if ( frameJson.kind == '1' ) // regular selected
				{
						/* continue with the current segment, start next lap
						 * with the current frame */
						this.prevFrame = frameJson;
						type = Session.frametype.LAP;
				}
				else if ( frameJson.kind == '2' ) // SPLIT
				{
						/* terminate current segment / start new */
						this.segments.push( new Segment() );
						/* start next segment/lap with the same frame */
						this.prevFrame = frameJson;
						type = Session.frametype.SPLIT;
				}
				else if ( frameJson.kind == '3' ) // END
				{
						/* terminate current segment / start new */
						this.segments.push( new Segment() );
						/* end frame is not used for the next lap */
						this.prevFrame = null;
						type = Session.frametype.FINISH;
				}
		}
		else
		{
				if ( frameJson.kind == '3' )
				{
						/* lap is not started with END */
						this.prevFrame = null;
						type = Session.frametype.NONE;
				}
				else
				{
						this.prevFrame = frameJson;
						type = Session.frametype.START;
				}
		}
		return type;
}

/* terminate session after feeding frames */
Session.prototype.finalize = function ()
{
		if ( this.segment().laps.length == 0 )
		{
				this.segments.pop();
		}
		this.prevFrame = null;
}

function BuildSessionFromPage ()
{
		var lapLen = $( document ).data( "json" ).lap_length;
		var sess = new Session( lapLen );
		var elems = document.getElementsByClassName( "selected" );
		for ( var i = 0, item ; item = elems[i] ; i++ )
		{
				var kind = $( item ).attr( "kind" );
				var json = $( item ).data( "json" );
				json.kind = kind; // update kind
				sess.feedFrame( json );
		}
		sess.finalize();
		return sess;
}

function InfoBlob ( text )
{
		var blob = $( "<div>" ).addClass( "blob" ).text( text );
		var info = $( '<span>' ).addClass( 'info' ).addClass( 'hidden' ).text( '+' ).append( blob );
		blob.click( function( ev ) { ev.stopPropagation(); } );
		info.click( function() {
				if ( $( this ).hasClass( 'hidden' ) )
		{
				$( this ).contents().first()[0].textContent = '\u2A2F';
				$( this ).find( ".blob" ).show();
				$( this ).removeClass( 'hidden' );
		}
				else
		{
				$( this ).contents().first()[0].textContent = '+';
				$( this ).find( ".blob" ).hide();
				$( this ).addClass( 'hidden' );
		}
		} );
		/* a relative element anchors the absolute info
		 * button ==> allows tweaking the position */
		return $( '<span style="position: relative">' ).append( info );
}

function UpdateCalculatedTimes ()
{
		var lapLen = $( document ).data( "json" ).lap_length;
		var sess = new Session( lapLen );

		$( ".lapinfo" ).remove();
		var elems = document.getElementsByClassName( "selected" );
		for ( var i = 0, item ; item = elems[i] ; i++ )
		{
				var kind = $( item ).attr( "kind" );
				var json = $( item ).data( "json" );
				json.kind = kind; // update kind from the DOM element!

				var type = sess.feedFrame( json );
				if ( i == elems.length - 1 && kind == '1' && type == Session.frametype.LAP ) type = Session.frametype.FINISH;

				var label = "";
				if ( type == Session.frametype.START )
				{
						label = "Start " + FormatTime( 0 );
				}
				else if ( type == Session.frametype.LAP )
				{
						label = "Lap" + sess.segment().lastLap().number
								+ " " + FormatTime( sess.segment().getTime() );
				}
				else if ( type == Session.frametype.SPLIT )
				{
						label = "Split " + FormatTime( sess.segment( -1 ).getTime() )
								+ "-" + FormatTime( 0 );
				}
				else if ( type == Session.frametype.FINISH )
				{
						label = "Finish " + FormatTime( sess.segment().getTime() );
				}

				$( item.getElementsByClassName( "reltime" )[0] ).text( label );
		}
		sess.finalize();

		/* build lap data table from sess */

		for ( var i = 0, segment ; segment = sess.segments[ i ] ; i++ )
		{
				var lapTimes = '';
				var lapSpeeds = '';
				for ( var j = 0, lap ; lap = segment.laps[ j ] ; j++ )
				{
						var row = $( "<tr>" ).addClass( "lapinfo" );
						if ( i > 0 && j == 0 ) row.addClass( "segmentstart" );
						$( "<td>" ).text( lap.number ).appendTo( row );
						$( "<td>" ).text( Dist( lap.number * sess.lapLength ) ).appendTo( row );
						var t = FormatTime( segment.getTime( j+1 ) );
						var time = $( "<td>" ).text( t ).appendTo( row );
						var lapT = FormatTime( lap.getTime() );
						$( "<td>" ).text( lapT ).appendTo( row );
						var lapS = lap.getSpeed();
						$( "<td>" ).text( lapS ).appendTo( row );
						var speed = $( "<td>" ).text( segment.getSpeed( j+1 ) ).appendTo( row );

						lapTimes += ( j != 0 ? ', ' : '' ) + lapT;
						lapSpeeds += ( j != 0 ? ', ' : '' ) + lapS;

						if ( j == segment.laps.length - 1 )
						{
								lapTimes = t + ' (' + lapTimes + ')';
								lapSpeeds = segment.getSpeed() + ' (' + lapSpeeds + ')';

								time.addClass( "finalresult" );
								speed.addClass( "finalresult" );
								time.append( InfoBlob( lapTimes ) );
								speed.append( InfoBlob( lapSpeeds ) );
								//time.append( info );
						}
						row.appendTo( "#times" );
				}
		}

		if ( sess.segments.length )
		{
				$( ".resulttable" ).show();
		}
		else
		{
				$( ".resulttable" ).hide();
		}
}

//------------------------------------------------------------------------------------------

function BuildSessionFromJson ( json )
{
		var sess = new Session( json.lap_length );
		for ( var i = 0, item ; item = json.frames[ i ] ; i++ )
		{
				sess.feedFrame( item );
		}
		sess.finalize();
		return sess;
}

function UpdateSessionRow ( name )
{
		return function ( data ) {
				var sess = BuildSessionFromJson( data );

				var row = $( "." + name );
				row.click( ToggleSession( name ) );
				/*
				row.mousedown( function ( ev ) {
						if ( ev.shiftKey )
						{
								ev.preventDefault();
						}
				} );
				*/

				var rowcopy = row.clone( true );
				for ( var i = 0, segment ; segment = sess.segments[ i ] ; i++ )
				{
						if ( i > 0 )
						{
								row.after( rowcopy );
								row = rowcopy;
								rowcopy = row.clone( true );
						}
						row.find( ".segment" ).text( i + 1 );
						row.find( ".laps" ).text( segment.laps.length );
						row.find( ".dist" ).text( Dist( segment.getDistance() ) );
						row.find( ".time" ).text( FormatTime( segment.getTime() ) );
						row.find( ".speed" ).text( segment.getSpeed() );
				}
				$( "#sessionTable" ).trigger( "update" );
				$( "#sessionTable" ).trigger( "refreshWidgets" );
		}
}

/* 'omit' can be undefined */
function ClearSelections ( omit )
{
		for ( var key in window.MyData.selected )
		{
				if ( key !== omit && window.MyData.selected[ key ] )
				{
						DeselectSession( key );
				}
		}
}

function DeselectSession ( name )
{
		jss.remove( "." + name );
		jss.remove( "." + name + " a" );
		$( "." + name ).removeClass( "selected" );
		delete window.MyData.selected[ name ];
}

function SelectSession ( name )
{
		jss.set( "." + name, {
				"background-color" : "#aa0"
				, "color" : "white"
		} );
		jss.set( "." + name + " a", { "color" : "white" } );
		$( "." + name ).addClass( "selected" );
		window.MyData.selected[ name ] = true;
}

function ToggleSession ( name )
{
		return function ( ev )
		{
				if ( ! ev.ctrlKey )
				{
						if ( Object.keys( window.MyData.selected ).length == 1 )
						{
								ClearSelections( name );
						}
						else
						{
								ClearSelections();
						}
				}
				if ( window.MyData.selected[ name ] === true )
				{
						DeselectSession( name );
				}
				else
				{
						SelectSession( name );
				}
		}
}

