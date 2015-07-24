/**
 *
 **/

#include "Listener.h"
#include "ScriptedCommandInterpreter.h"

namespace Carta
{
namespace Core
{
namespace ScriptedClient
{
ScriptedCommandInterpreter::ScriptedCommandInterpreter( int port, QObject * parent )
    : QObject( parent )
{
    qDebug() << "ScriptedCommandInterpreter starting on port:" << port;

    m_messageListener.reset( new MessageListener( port, this ) );

    connect( m_messageListener.get(), & MessageListener::received,
             this, & ScriptedCommandInterpreter::tagMessageReceivedCB );

    connect( m_messageListener.get(), & MessageListener::receivedAsync,
             this, & ScriptedCommandInterpreter::asyncMessageReceivedCB );
}

/// this is just a quick demo how to listen to TagMessage, convert them to Json,
/// extract info from Json, and pack results back into Json, then to TagMessage...
///
/// it could probably use some error checking, like asserts etc...
void
ScriptedCommandInterpreter::tagMessageReceivedCB( TagMessage tm )
{
    m_scriptFacade = ScriptFacade::getInstance();
    if ( tm.tag() != "json" ) {
        qWarning() << "I don't handle tag" << tm.tag();
        return;
    }
    JsonMessage jm = JsonMessage::fromTagMessage( tm );
    if ( ! jm.doc().isObject() ) {
        qWarning() << "Received json is not object...";
        return;
    }
    QJsonObject jo = jm.doc().object();
    QString cmd = jo["cmd"].toString().toLower();
    auto args = jo["args"].toObject();
    QJsonObject rjo;
    QStringList result;
    QString key = "result";

    /// application commands

    if ( cmd == "getcolormapviews" ) {
        result = m_scriptFacade->getColorMapViews();
    }

    else if ( cmd == "getimageviews" ) {
        result = m_scriptFacade->getImageViews();
    }

    else if ( cmd == "getanimatorviews" ) {
        result = m_scriptFacade->getAnimatorViews();
    }

    else if ( cmd == "gethistogramviews" ) {
        result = m_scriptFacade->getHistogramViews();
    }

    else if ( cmd == "getstatisticsviews" ) {
        result = m_scriptFacade->getStatisticsViews();
    }

    else if ( cmd == "setanalysislayout" ) {
        result = m_scriptFacade->setAnalysisLayout();
    }

    else if ( cmd == "setimagelayout" ) {
        result = m_scriptFacade->setImageLayout();
    }

    else if ( cmd == "setcustomlayout" ) {
        int rows = args["nrows"].toInt();
        int columns = args["ncols"].toInt();
        result = m_scriptFacade->setCustomLayout(rows, columns);
    }

    else if ( cmd == "setplugins" ) {
        QString plugins = args["plugins"].toString();
        QStringList pluginsList = plugins.split(' ');
        result = m_scriptFacade->setPlugins(pluginsList);
    }

    else if ( cmd == "addlink" ) {
        QString source = args["sourceView"].toString();
        QString dest = args["destView"].toString();
        result = m_scriptFacade->addLink(source, dest);
    }

    else if ( cmd == "removelink" ) {
        QString source = args["sourceView"].toString();
        QString dest = args["destView"].toString();
        result = m_scriptFacade->removeLink(source, dest);
    }

    else if ( cmd == "savesnapshot" ) {
        QString sessionId = args["sessionId"].toString();
        QString saveName = args["saveName"].toString();
        bool saveLayout = args["saveLayout"].toBool();
        bool savePreferences = args["savePreferences"].toBool();
        bool saveData = args["saveData"].toBool();
        QString description = args["description"].toString();
        result = m_scriptFacade->saveSnapshot(sessionId, saveName, saveLayout, savePreferences, saveData, description);
    }

    else if ( cmd == "getsnapshots" ) {
        QString sessionId = args["sessionId"].toString();
        result = m_scriptFacade->getSnapshots(sessionId);
    }

    else if ( cmd == "getsnapshotobjects" ) {
        QString sessionId = args["sessionId"].toString();
        result = m_scriptFacade->getSnapshotObjects(sessionId);
    }

    else if ( cmd == "deletesnapshot" ) {
        QString sessionId = args["sessionId"].toString();
        QString saveName = args["saveName"].toString();
        result = m_scriptFacade->deleteSnapshot(sessionId, saveName);
    }

    else if ( cmd == "restoresnapshot" ) {
        QString sessionId = args["sessionId"].toString();
        QString saveName = args["saveName"].toString();
        result = m_scriptFacade->restoreSnapshot(sessionId, saveName);
    }

    else if ( cmd == "getcolormaps" ) {
        result = m_scriptFacade->getColorMaps();
    }

    /// colormap commands

    else if ( cmd == "setcolormap" ) {
        QString colormapId = args["colormapId"].toString();
        QString colormapName = args["colormapName"].toString();
        result = m_scriptFacade->setColorMap( colormapId, colormapName );
    }

    else if ( cmd == "reversecolormap" ) {
        QString colormapId = args["colormapId"].toString();
        QString reverseString = args["reverseString"].toString().toLower();
        result = m_scriptFacade->reverseColorMap( colormapId, reverseString );
    }

    else if ( cmd == "invertcolormap" ) {
        QString colormapId = args["colormapId"].toString();
        QString invertString = args["invertString"].toString().toLower();
        result = m_scriptFacade->invertColorMap( colormapId, invertString );
    }

    else if ( cmd == "setcolormix" ) {
        QString colormapId = args["colormapId"].toString();
        double red = args["red"].toDouble();
        double green = args["green"].toDouble();
        double blue = args["blue"].toDouble();
        result = m_scriptFacade->setColorMix( colormapId, red, green, blue );
    }

    else if ( cmd == "setgamma" ) {
        QString colormapId = args["colormapId"].toString();
        double gamma = args["gammaValue"].toDouble();
        result = m_scriptFacade->setGamma( colormapId, gamma );
    }

    else if ( cmd == "setdatatransform" ) {
        QString colormapId = args["colormapId"].toString();
        QString transform = args["transform"].toString();
        result = m_scriptFacade->setDataTransform( colormapId, transform );
    }

    /// image/controller commands

    else if ( cmd == "loadfile" ) {
        QString imageView = args["imageView"].toString();
        QString fileName = args["fname"].toString();
        result = m_scriptFacade->loadFile( imageView, fileName );
    }

    else if ( cmd == "loadlocalfile" ) {
        QString imageView = args["imageView"].toString();
        QString fileName = args["fname"].toString();
        result = m_scriptFacade->loadLocalFile( imageView, fileName );
    }

    else if ( cmd == "getlinkedcolormaps" ) {
        QString imageView = args["imageView"].toString();
        result = m_scriptFacade->getLinkedColorMaps( imageView );
    }

    else if ( cmd == "getlinkedanimators" ) {
        QString imageView = args["imageView"].toString();
        result = m_scriptFacade->getLinkedAnimators( imageView );
    }

    else if ( cmd == "getlinkedhistograms" ) {
        QString imageView = args["imageView"].toString();
        result = m_scriptFacade->getLinkedHistograms( imageView );
    }

    else if ( cmd == "getlinkedstatistics" ) {
        QString imageView = args["imageView"].toString();
        result = m_scriptFacade->getLinkedStatistics( imageView );
    }

    else if ( cmd == "setclipvalue" ) {
        QString imageView = args["imageView"].toString();
        double clipValue = args["clipValue"].toDouble();
        result = m_scriptFacade->setClipValue( imageView, clipValue );
    }

    else if ( cmd == "saveimage" ) {
        QString imageView = args["imageView"].toString();
        QString filename = args["filename"].toString();
        result = m_scriptFacade->saveImage( imageView, filename );
    }

    else if ( cmd == "centeronpixel" ) {
        QString imageView = args["imageView"].toString();
        double x = args["xval"].toDouble();
        double y = args["yval"].toDouble();
        result = m_scriptFacade->centerOnPixel( imageView, x, y );
    }

    else if ( cmd == "setzoomlevel" ) {
        QString imageView = args["imageView"].toString();
        double zoomLevel = args["zoomLevel"].toDouble();
        result = m_scriptFacade->setZoomLevel( imageView, zoomLevel );
    }

    else if ( cmd == "getzoomlevel" ) {
        QString imageView = args["imageView"].toString();
        result = m_scriptFacade->getZoomLevel( imageView );
    }

    else if ( cmd == "getimagedimensions" ) {
        QString imageView = args["imageView"].toString();
        result = m_scriptFacade->getImageDimensions( imageView );
    }

    else if ( cmd == "getoutputsize" ) {
        QString imageView = args["imageView"].toString();
        result = m_scriptFacade->getOutputSize( imageView );
    }

    else if ( cmd == "getintensity" ) {
        QString imageView = args["imageView"].toString();
        int frameLow = args["frameLow"].toInt();
        int frameHigh = args["frameHigh"].toInt();
        double percentile = args["percentile"].toDouble();
        result = m_scriptFacade->getIntensity( imageView, frameLow, frameHigh, percentile );
    }

    else if ( cmd == "getpixelcoordinates" ) {
        QString imageView = args["imageView"].toString();
        double ra = args["ra"].toDouble();
        double dec = args["dec"].toDouble();
        result = m_scriptFacade->getPixelCoordinates( imageView, ra, dec );
    }

    else if ( cmd == "getpixelvalue" ) {
        QString imageView = args["imageView"].toString();
        double x = args["x"].toDouble();
        double y = args["y"].toDouble();
        result = m_scriptFacade->getPixelValue( imageView, x, y );
    }

    else if ( cmd == "getpixelunits" ) {
        QString imageView = args["imageView"].toString();
        result = m_scriptFacade->getPixelUnits( imageView );
    }

    else if ( cmd == "getcoordinates" ) {
        QString imageView = args["imageView"].toString();
        double x = args["x"].toDouble();
        double y = args["y"].toDouble();
        QString systemStr = args["system"].toString().toLower();
        Carta::Lib::KnownSkyCS system;
        if ( systemStr == "j2000" ){
            system = Carta::Lib::KnownSkyCS::J2000;
        }
        else if ( systemStr == "b1950" ){
            system = Carta::Lib::KnownSkyCS::B1950;
        }
        else if ( systemStr == "icrs" ){
            system = Carta::Lib::KnownSkyCS::ICRS;
        }
        else if ( systemStr == "galactic" ){
            system = Carta::Lib::KnownSkyCS::Galactic;
        }
        else if ( systemStr == "ecliptic" ){
            system = Carta::Lib::KnownSkyCS::Ecliptic;
        }
        else {
            system = Carta::Lib::KnownSkyCS::Error;
        }
        if ( system != Carta::Lib::KnownSkyCS::Error ) {
            result = m_scriptFacade->getCoordinates( imageView, x, y, system );
        }
        else {
            result = QStringList( "error" );
            result.append( "Invalid coordinate system: " + systemStr );
        }
    }

    else if ( cmd == "getimagenames" ) {
        QString imageView = args["imageView"].toString();
        result = m_scriptFacade->getImageNames( imageView );
    }

    else if ( cmd == "closeimage" ) {
        QString imageView = args["imageView"].toString();
        QString imageName = args["imageName"].toString();
        result = m_scriptFacade->closeImage( imageView, imageName );
    }

    /// animator commands

    else if ( cmd == "setchannel" ) {
        QString animatorView = args["animatorView"].toString();
        int channel = args["channel"].toInt();
        result = m_scriptFacade->setChannel( animatorView, channel );
    }

    else if ( cmd == "setimage" ) {
        QString animatorView = args["animatorView"].toString();
        int image = args["image"].toInt();
        result = m_scriptFacade->setImage( animatorView, image );
    }

    else if ( cmd == "showimageanimator" ) {
        QString animatorView = args["animatorView"].toString();
        result = m_scriptFacade->showImageAnimator( animatorView );
    }

    /// histogram commands

    else if ( cmd == "setclipbuffer" ) {
        QString histogramView = args["histogramView"].toString();
        int bufferAmount = args["bufferAmount"].toInt();
        result = m_scriptFacade->setClipBuffer( histogramView, bufferAmount );
    }

    else if ( cmd == "setuseclipbuffer" ) {
        QString histogramView = args["histogramView"].toString();
        QString useBuffer = args["useBuffer"].toString().toLower();
        result = m_scriptFacade->setUseClipBuffer( histogramView, useBuffer );
    }

    else if ( cmd == "setcliprange" ) {
        QString histogramView = args["histogramView"].toString();
        double minRange = args["minRange"].toDouble();
        double maxRange = args["maxRange"].toDouble();
        result = m_scriptFacade->setClipRange( histogramView, minRange, maxRange );
    }

    else if ( cmd == "applyclips" ) {
        QString histogramView = args["histogramView"].toString();
        double clipMinValue = args["clipMinValue"].toDouble();
        double clipMaxValue = args["clipMaxValue"].toDouble();
        QString modeStr = args["modeStr"].toString();
        result = m_scriptFacade->applyClips( histogramView, clipMinValue, clipMaxValue, modeStr );
    }

    else if ( cmd == "setbincount" ) {
        QString histogramView = args["histogramView"].toString();
        int binCount = args["binCount"].toInt();
        result = m_scriptFacade->setBinCount( histogramView, binCount );
    }

    else if ( cmd == "setbinwidth" ) {
        QString histogramView = args["histogramView"].toString();
        double binWidth = args["binWidth"].toDouble();
        result = m_scriptFacade->setBinWidth( histogramView, binWidth );
    }

    else if ( cmd == "setplanemode" ) {
        QString histogramView = args["histogramView"].toString();
        QString planeMode = args["planeMode"].toString();
        result = m_scriptFacade->setPlaneMode( histogramView, planeMode );
    }

    else if ( cmd == "setplanerange" ) {
        QString histogramView = args["histogramView"].toString();
        double minPlane = args["minPlane"].toDouble();
        double maxPlane = args["maxPlane"].toDouble();
        result = m_scriptFacade->setPlaneRange( histogramView, minPlane, maxPlane );
    }

    else if ( cmd == "setchannelunit" ) {
        QString histogramView = args["histogramView"].toString();
        QString unit = args["unit"].toString();
        result = m_scriptFacade->setChannelUnit( histogramView, unit );
    }

    else if ( cmd == "setgraphstyle" ) {
        QString histogramView = args["histogramView"].toString();
        QString graphStyle = args["graphStyle"].toString();
        result = m_scriptFacade->setGraphStyle( histogramView, graphStyle );
    }

    else if ( cmd == "setlogcount" ) {
        QString histogramView = args["histogramView"].toString();
        QString logCount = args["logCount"].toString().toLower();
        result = m_scriptFacade->setLogCount( histogramView, logCount );
    }

    else if ( cmd == "setcolored" ) {
        QString histogramView = args["histogramView"].toString();
        QString colored = args["colored"].toString().toLower();
        result = m_scriptFacade->setColored( histogramView, colored );
    }

    else if ( cmd == "savehistogram" ) {
        QString histogramView = args["histogramView"].toString();
        QString filename = args["filename"].toString();
        int width = args["width"].toInt();
        int height = args["height"].toInt();
        result = m_scriptFacade->saveHistogram( histogramView, filename, width, height );
    }

    /// grid commands

    else if ( cmd == "setgridaxescolor" ) {
        QString imageView = args["imageView"].toString();
        int red = args["red"].toInt();
        int green = args["green"].toInt();
        int blue = args["blue"].toInt();
        result = m_scriptFacade->setGridAxesColor( imageView, red, green, blue );
    }

    else if ( cmd == "setgridaxesthickness" ) {
        QString imageView = args["imageView"].toString();
        int thickness = args["thickness"].toInt();
        result = m_scriptFacade->setGridAxesThickness( imageView, thickness );
    }

    else if ( cmd == "setgridaxestransparency" ) {
        QString imageView = args["imageView"].toString();
        int transparency = args["transparency"].toInt();
        result = m_scriptFacade->setGridAxesTransparency( imageView, transparency );
    }

    else if ( cmd == "setgridapplyall" ) {
        QString imageView = args["imageView"].toString();
        bool applyAll = args["applyAll"].toBool();
        result = m_scriptFacade->setGridApplyAll( imageView, applyAll );
    }

    else if ( cmd == "setgridcoordinatesystem" ) {
        QString imageView = args["imageView"].toString();
        QString coordSystem = args["coordSystem"].toString();
        result = m_scriptFacade->setGridCoordinateSystem( imageView, coordSystem );
    }

    else if ( cmd == "setgridfontfamily" ) {
        QString imageView = args["imageView"].toString();
        QString fontFamily = args["fontFamily"].toString();
        result = m_scriptFacade->setGridFontFamily( imageView, fontFamily );
    }

    else if ( cmd == "setgridfontsize" ) {
        QString imageView = args["imageView"].toString();
        int fontSize = args["fontSize"].toInt();
        result = m_scriptFacade->setGridFontSize( imageView, fontSize );
    }

    else if ( cmd == "setgridcolor" ) {
        QString imageView = args["imageView"].toString();
        int redAmount = args["redAmount"].toInt();
        int greenAmount = args["greenAmount"].toInt();
        int blueAmount = args["blueAmount"].toInt();
        result = m_scriptFacade->setGridColor( imageView, redAmount, greenAmount, blueAmount );
    }

    else if ( cmd == "setgridspacing" ) {
        QString imageView = args["imageView"].toString();
        double spacing = args["spacing"].toDouble();
        result = m_scriptFacade->setGridSpacing( imageView, spacing );
    }

    else if ( cmd == "setgridthickness" ) {
        QString imageView = args["imageView"].toString();
        int thickness = args["thickness"].toInt();
        result = m_scriptFacade->setGridThickness( imageView, thickness );
    }

    else if ( cmd == "setgridtransparency" ) {
        QString imageView = args["imageView"].toString();
        int transparency = args["transparency"].toInt();
        result = m_scriptFacade->setGridTransparency( imageView, transparency );
    }

    else if ( cmd == "setgridlabelcolor" ) {
        QString imageView = args["imageView"].toString();
        int redAmount = args["redAmount"].toInt();
        int greenAmount = args["greenAmount"].toInt();
        int blueAmount = args["blueAmount"].toInt();
        result = m_scriptFacade->setGridLabelColor( imageView, redAmount, greenAmount, blueAmount );
    }

    else if ( cmd == "setshowgridlines" ) {
        QString imageView = args["imageView"].toString();
        bool showGridLines = args["showGridLines"].toBool();
        result = m_scriptFacade->setShowGridLines( imageView, showGridLines );
    }

    /// commands for testing

    else if ( cmd == "fakecommand" ) {
        QString data = args["data"].toString();
        result.append("Fake command received");
    }

    else {
        qDebug() << "Unknown command, sending error back";
        key = "error";
        result.append("Unknown command");
    }

    if ( result[0] == "error" ) {
        key = "error";
    }
    rjo.insert( key, QJsonValue::fromVariant( result ) );
    JsonMessage rjm = JsonMessage( QJsonDocument( rjo ) );
    m_messageListener->send( rjm.toTagMessage() );
} // tagMessageReceivedCB

void
ScriptedCommandInterpreter::asyncMessageReceivedCB( TagMessage tm )
{
    m_scriptFacade = ScriptFacade::getInstance();
    if ( tm.tag() != "async" ) {
        qWarning() << "I don't handle tag" << tm.tag();
        return;
    }
    JsonMessage jm = JsonMessage::fromTagMessage( tm );
    if ( ! jm.doc().isObject() ) {
        qWarning() << "Received json is not object...";
        return;
    }

    connect( m_scriptFacade, & ScriptFacade::saveImageResult,
             this, & ScriptedCommandInterpreter::saveImageResultCB );

    QJsonObject jo = jm.doc().object();
    QString cmd = jo["cmd"].toString().toLower();
    auto args = jo["args"].toObject();
    if ( cmd == "savefullimage" ) {
        QString imageView = args["imageView"].toString();
        QString filename = args["filename"].toString();
        int width = args["width"].toInt();
        int height = args["height"].toInt();
        double scale = args["scale"].toDouble();
        QString aspectStr = args["aspectRatioMode"].toString().toLower();
        Qt::AspectRatioMode aspectRatioMode;
        if ( aspectStr == "keep" ){
            aspectRatioMode = Qt::KeepAspectRatio;
        }
        else if ( aspectStr == "expand" ){
            aspectRatioMode = Qt::KeepAspectRatioByExpanding;
        }
        else {
            aspectRatioMode = Qt::IgnoreAspectRatio;
        }
        m_scriptFacade->saveFullImage( imageView, filename, width, height, scale, aspectRatioMode );
    }

} // asyncMessageReceivedCB

void ScriptedCommandInterpreter::saveImageResultCB( bool saveResult ){
    disconnect( m_scriptFacade, & ScriptFacade::saveImageResult,
             this, & ScriptedCommandInterpreter::saveImageResultCB );

    QJsonObject rjo;
    QStringList result("");
    QString key = "result";
    if ( saveResult == false ) {
        key = "error";
        result[0] = "Could not save image.";
    }
    rjo.insert( key, QJsonValue::fromVariant( result ) );
    JsonMessage rjm = JsonMessage( QJsonDocument( rjo ) );
    m_messageListener->send( rjm.toTagMessage() );
}

}
}
}
