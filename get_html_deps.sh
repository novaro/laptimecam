#!/bin/sh
# Download static html dependencies to html/import subdirectory.

HTTP_IMPORT="\
	https://raw.githubusercontent.com/Mottie/tablesorter/c1ce0768d76d3631bb5a5b838e1ee8d4103f5f73/js/jquery.tablesorter.js;jquery.tablesorter.js \
	https://raw.githubusercontent.com/Box9/jss/1faffdf9373149a8874fc8df0ba0512b782dba3f/jss.js;jss.js \
	http://code.jquery.com/jquery-2.1.1.js;jquery.js \
	https://raw.githubusercontent.com/w0ng/googlefontdirectory/ee8268dbf4a4a19d7ea3d252f4819a0fbbd96f3a/fonts/signika/Signika-Regular.ttf;Signika-Regular.ttf"

mkdir -p html/import
for url in $HTTP_IMPORT ; do
	curl ${url%;*} > html/import/${url#*;}
done
