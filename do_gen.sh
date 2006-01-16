top_srcdir=$PWD
export PYTHONPATH=$PYTHONPATH:$top_srcdir/tools

cd $top_srcdir/test

echo Generating TpMediaSessionHandler files ...
python $top_srcdir/tools/gengobject.py $top_srcdir/xml/tp-media-session-handler.xml TpMediaSessionHandler
echo Generating TpMediaStreamHandler files ...
python $top_srcdir/tools/gengobject.py $top_srcdir/xml/tp-media-session-handler.xml TpMediaSessionHandler
echo Generating error enums ...
python $top_srcdir/tools/generrors.py

cd $top_srcdir/src

echo Generating TpChannelHandler files ...
python $top_srcdir/tools/gengobject.py $top_srcdir/xml/tp-channel-handler.xml TpChannelHandler

