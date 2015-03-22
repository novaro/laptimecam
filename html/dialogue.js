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

function Dialogue ( dialogue_selector, activate_selector, accept_label, pre_hook, accept_function )
{
		var content = $( dialogue_selector );
		/* setup dialogue */

		var outer = $( "<div>" ).addClass( "dialogue" );
		var inner = $( "<div>" ).addClass( "dialogueInner" );

		var accept = $( "<span>" ).html( accept_label ).addClass( "button" ).click( function() {
				outer.hide();
				accept_function();
		} );
		var cancel = $( "<span>" ).html( "Cancel" ).addClass( "button" ).click( function() {
				$( ".blinds" ).hide();
				outer.hide();
		} );

		inner.append( content );
		inner.append( $( "<br>" ) );
		inner.append( $( "<span>" ).append( accept ).append( document.createTextNode( " " ) ).append( cancel ) );

		outer.append( inner );

		outer.appendTo( "body" );
		/* add blinds layer if it's missing */
		if ( $( ".blinds" ).length == 0 )
		{
				$( "<div>" ).addClass( "blinds" ).appendTo( "body" );
		}

		$( activate_selector ).click( function() {
				if ( pre_hook() )
				{
						$( ".blinds" ).show();
						outer.show();
				}
		} );

		content.show(); // show in case the content element was hidden
}

