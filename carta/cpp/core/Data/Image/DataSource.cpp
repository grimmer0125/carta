#include "DataSource.h"
#include "CoordinateSystems.h"
#include "Data/Colormap/Colormaps.h"
#include "Globals.h"
#include "PluginManager.h"
#include "GrayColormap.h"
#include "CartaLib/IImage.h"
#include "Data/Util.h"
#include "Data/Colormap/TransformsData.h"
#include "CartaLib/Hooks/LoadAstroImage.h"
#include "CartaLib/Hooks/GetPersistentCache.h"
#include "CartaLib/PixelPipeline/CustomizablePixelPipeline.h"
#include "CartaLib/IPCache.h"
#include "../../ImageRenderService.h"
#include "../../Algorithms/quantileAlgorithms.h"
#include "../../Algorithms/cacheUtils.h"
#include <QDebug>
#include <sys/time.h>
#include <numeric>
#include <ctime>
using Carta::Lib::AxisInfo;
using Carta::Lib::AxisDisplayInfo;

namespace Carta {

namespace Data {

const QString DataSource::DATA_PATH = "file";
const QString DataSource::CLASS_NAME = "DataSource";
const double DataSource::ZOOM_DEFAULT = 1.0;
const int DataSource::INDEX_LOCATION = 0;
const int DataSource::INDEX_INTENSITY = 1;
const int DataSource::INDEX_PERCENTILE = 2;
const int DataSource::INDEX_FRAME_LOW = 3;
const int DataSource::INDEX_FRAME_HIGH = 4;

CoordinateSystems* DataSource::m_coords = nullptr;

DataSource::DataSource() :
    m_image( nullptr ),
    m_permuteImage( nullptr),
    m_cachedPercentiles(100),
    m_axisIndexX( 0 ),
    m_axisIndexY( 1 ){
        m_cmapCacheSize = 1000;

        _initializeSingletons();

        //Initialize the rendering service
        m_renderService.reset( new Carta::Core::ImageRenderService::Service() );

        // assign a default colormap to the view
        auto rawCmap = std::make_shared < Carta::Core::GrayColormap > ();

        // initialize pixel pipeline
        m_pixelPipeline = std::make_shared < Carta::Lib::PixelPipeline::CustomizablePixelPipeline > ();
        m_pixelPipeline-> setInvert( false );
        m_pixelPipeline-> setReverse( false );
        m_pixelPipeline-> setColormap( std::make_shared < Carta::Core::GrayColormap > () );
        m_pixelPipeline-> setMinMax( 0, 1 );
        m_renderService-> setPixelPipeline( m_pixelPipeline, m_pixelPipeline-> cacheId());

        // initialize disk cache
        auto res = Globals::instance()-> pluginManager()
                   -> prepare < Carta::Lib::Hooks::GetPersistentCache > ().first();
        if ( res.isNull() || ! res.val() ) {
            qWarning( "Could not find a disk cache plugin." );
            m_diskCache = nullptr;
        }
        else {
            m_diskCache = res.val();
        }
}

std::vector<int> DataSource::_getPermOrder() const
{
    //Build a vector showing the permute order.
    int imageDim = m_image->dims().size();
    std::vector<int> indices( imageDim );
    indices[0] = m_axisIndexX;
    indices[1] = m_axisIndexY;
    int vectorIndex = 2;
    for ( int i = 0; i < imageDim; i++ ){
        if ( i != m_axisIndexX && i != m_axisIndexY ){
            indices[vectorIndex] = i;
            vectorIndex++;
        }
    }

    return indices;
}


int DataSource::_getFrameIndex( int sourceFrameIndex, const std::vector<int>& sourceFrames ) const {
    int frameIndex = 0;
    if (m_image ){
        AxisInfo::KnownType axisType = static_cast<AxisInfo::KnownType>( sourceFrameIndex );
        int axisIndex = Util::getAxisIndex( m_image, axisType );
        //The image doesn't have this particular axis.
        if ( axisIndex >=  0 ) {
            //The image has the axis so make the frame bounded by the image size.
            frameIndex = Carta::Lib::clamp( sourceFrames[sourceFrameIndex], 0, m_image-> dims()[axisIndex] - 1 );
        }
    }
    return frameIndex;
}

std::vector<int> DataSource::_fitFramesToImage( const std::vector<int>& sourceFrames ) const {
    int sourceFrameCount = sourceFrames.size();
    std::vector<int> outputFrames( sourceFrameCount );
    for ( int i = 0; i < sourceFrameCount; i++ ){
        outputFrames[i] = _getFrameIndex( i, sourceFrames );
    }
    return outputFrames;
}


std::vector<AxisInfo::KnownType> DataSource::_getAxisTypes() const {
    std::vector<AxisInfo::KnownType> types;
    CoordinateFormatterInterface::SharedPtr cf(
                   m_image-> metaData()-> coordinateFormatter()-> clone() );
    int axisCount = cf->nAxes();
    for ( int axis = 0 ; axis < axisCount; axis++ ) {
        const AxisInfo & axisInfo = cf-> axisInfo( axis );
        AxisInfo::KnownType axisType = axisInfo.knownType();
        if ( axisType != AxisInfo::KnownType::OTHER ){
            types.push_back( axisInfo.knownType() );
        }
    }
    return types;
}


std::vector<AxisInfo> DataSource::_getAxisInfos() const {
    std::vector<AxisInfo> Infos;
    CoordinateFormatterInterface::SharedPtr cf(
                   m_image-> metaData()-> coordinateFormatter()-> clone() );
    int axisCount = cf->nAxes();
    for ( int axis = 0 ; axis < axisCount; axis++ ) {
        const AxisInfo & axisInfo = cf-> axisInfo( axis );
        if ( axisInfo.knownType() != AxisInfo::KnownType::OTHER ){
            Infos.push_back( axisInfo );
        }
    }
    return Infos;
}


AxisInfo::KnownType DataSource::_getAxisType( int index ) const {
    AxisInfo::KnownType type = AxisInfo::KnownType::OTHER;
    CoordinateFormatterInterface::SharedPtr cf(
                       m_image-> metaData()-> coordinateFormatter()-> clone() );
    int axisCount = cf->nAxes();
    if ( index < axisCount && index >= 0 ){
        AxisInfo axisInfo = cf->axisInfo( index );
        type = axisInfo.knownType();
    }
    return type;
}

AxisInfo::KnownType DataSource::_getAxisXType() const {
    return _getAxisType( m_axisIndexX );
}

AxisInfo::KnownType DataSource::_getAxisYType() const {
    return _getAxisType( m_axisIndexY );
}

std::vector<AxisInfo::KnownType> DataSource::_getAxisZTypes() const {
    std::vector<AxisInfo::KnownType> zTypes;
    if ( m_image ){
        int imageDims = m_image->dims().size();
        for ( int i = 0; i < imageDims; i++ ){
            if ( i != m_axisIndexX && i!= m_axisIndexY ){
                AxisInfo::KnownType type = _getAxisType( i );
                if ( type != AxisInfo::KnownType::OTHER ){
                    zTypes.push_back( type );
                }
            }
        }
    }
    return zTypes;
}

QStringList DataSource::_getCoordinates( double x, double y,
        Carta::Lib::KnownSkyCS system, const std::vector<int>& frames ) const{
    std::vector<int> mFrames = _fitFramesToImage( frames );
    CoordinateFormatterInterface::SharedPtr cf( m_image-> metaData()-> coordinateFormatter()-> clone() );
    cf-> setSkyCS( system );
    int imageSize = m_image->dims().size();
    std::vector < double > pixel( imageSize, 0.0 );
    for ( int i = 0; i < imageSize; i++ ){
        if ( i == m_axisIndexX ){
            pixel[i] = x;
        }
        else if ( i == m_axisIndexY ){
            pixel[i] = y;
        }
        else {
            AxisInfo::KnownType axisType = _getAxisType( i );
            int axisIndex = static_cast<int>( axisType );
            pixel[i] = mFrames[axisIndex];
        }
    }
    QStringList list = cf-> formatFromPixelCoordinate( pixel );
    return list;
}

QString DataSource::_getSkyCS(){

    CoordinateFormatterInterface::SharedPtr cf(
            m_image-> metaData()-> coordinateFormatter()-> clone() );

    QString coordName = m_coords->getName( cf->skyCS() );
    return coordName;
}

// print the pixel value and x-y coordinate for the cursor on the image viewer
QString DataSource::_getCursorText(bool isAutoClip, double minPercent, double maxPercent, int mouseX, int mouseY,
        Carta::Lib::KnownSkyCS cs, const std::vector<int>& frames,
        double zoom, const QPointF& pan, const QSize& outputSize ){
    QString str;
    QTextStream out( & str );
    QPointF lastMouse( mouseX, mouseY );
    bool valid = false;
    QPointF imgPt = _getImagePt( lastMouse, zoom, pan, outputSize, &valid );
    if ( valid ){
        double imgX = imgPt.x();
        double imgY = imgPt.y();

        // set print out values with rounded imgX and imgY
        QString round_imgX = QString::number(imgX, 'f', 2);
        QString round_imgY = QString::number(imgY, 'f', 2);

        CoordinateFormatterInterface::SharedPtr cf(
                m_image-> metaData()-> coordinateFormatter()-> clone() );

        QString coordName = m_coords->getName( cf->skyCS() );
        //out << "Default sky cs:" << coordName << "\n";

        QString pixelValue = _getPixelValue( round(imgX), round(imgY), frames );
        QString pixelUnits = _getPixelUnits();

        out << "Pixel value = " << pixelValue << " " << pixelUnits << " at ";
        out << "(X, Y) = " << "("<< round_imgX << ", " << round_imgY << ")" << "\n";

        // get the Min. and Max. values of intensity for Quantile mode
        if (isAutoClip == true) {
            std::vector<int> mFrames = _fitFramesToImage( frames );
            std::shared_ptr<Carta::Lib::NdArray::RawViewInterface> view ( _getRawData( mFrames ) );
            std::vector<double> intensity = _getQuantileIntensityCache(view, minPercent, maxPercent, frames);

            // set print out values with rounded intensities
            QString sci_intensityMin = QString::number(intensity[0], 'E', 3);
            QString sci_intensityMax = QString::number(intensity[1], 'E', 3);

            double percent = (maxPercent - minPercent)*100;
            out << "<span style=\"color: #000000;\">bounds for "
                << percent << "% clipping per frame: "
                << "[" << sci_intensityMin
                << ", " << sci_intensityMax << "] "
                << "</span>"
                << "\n";
        }

        cf-> setSkyCS( cs );
        out << "[ " << m_coords->getName( cs ) << " ] ";
        std::vector <AxisInfo> ais;
        for ( int axis = 0 ; axis < cf->nAxes() ; axis++ ) {
            const AxisInfo & ai = cf-> axisInfo( axis );
            ais.push_back( ai );
        }

        QStringList coordList = _getCoordinates( imgX, imgY, cs, frames);
        for ( size_t i = 0 ; i < ais.size() ; i++ ) {
            if(ais[i].knownType() == Carta::Lib::AxisInfo::KnownType::SPECTRAL){
                out << coordList[i] << " ";
            }
            else{
                out << ais[i].shortLabel().html() << ":" << coordList[i] << " ";
            }
        }
        out << "\n";

        str.replace( "\n", "<br />" );
    }
    return str;
}

QPointF DataSource::_getCenter() const{
    QPointF center( nan(""), nan(""));
    if ( m_permuteImage != nullptr ){
         double xCenter =  m_permuteImage-> dims()[0] / 2.0;
         double yCenter = m_permuteImage-> dims()[1] / 2.0;
         // This is due to casa uses [0,0] as the center of the first pixel,
         // so there is 0.5 (image pixel coordinate, not screen pixel coordinate)
         // shift for the center of the whole image
         xCenter -= 0.5;
         yCenter -= 0.5;
         center.setX( xCenter );
         center.setY( yCenter );
     }
    return center;
}

std::vector<AxisDisplayInfo> DataSource::_getAxisDisplayInfo() const {
    std::vector<AxisDisplayInfo> axisInfo;
    //Note that permutations are 1-based whereas the axis
    //index is zero-based.
    if ( m_image ){
        int imageSize = m_image->dims().size();
        axisInfo.resize( imageSize );

        //Indicate the display axes by  putting -1 in for the display frames.
        //We will later fill in fixed frames for the other axes.
        axisInfo[m_axisIndexX].setFrame( -1 );
        axisInfo[m_axisIndexY].setFrame( -1 );

        //Indicate the new axis order.
        axisInfo[m_axisIndexX].setPermuteIndex( 0 );
        axisInfo[m_axisIndexY].setPermuteIndex( 1 );
        int availableIndex = 2;
        for ( int i = 0; i < imageSize; i++ ){
            axisInfo[i].setFrameCount( m_image->dims()[i] );
            axisInfo[i].setAxisType( _getAxisType( i ) );
            if ( i != m_axisIndexX && i != m_axisIndexY ){
                axisInfo[i].setPermuteIndex( availableIndex );
                availableIndex++;
            }
        }
    }
    return axisInfo;
}

QPointF DataSource::_getImagePt( const QPointF& screenPt, double zoom, const QPointF& pan,
            const QSize& outputSize, bool* valid ) const {
    QPointF imagePt;
    if ( m_image ){
        imagePt = m_renderService-> screen2image (screenPt, pan, zoom, outputSize);
        *valid = true;
    }
    else {
        *valid = false;
    }
    return imagePt;
}

QString DataSource::_getPixelValue( double x, double y, const std::vector<int>& frames ) const {
    QString pixelValue( "" );
    int valX = (int)(round(x));
    int valY = (int)(round(y));
    if ( valX >= 0 && valX < m_image->dims()[m_axisIndexX] && valY >= 0 && valY < m_image->dims()[m_axisIndexY] ) {
        Carta::Lib::NdArray::RawViewInterface* rawData = _getRawData( frames );
        if ( rawData != nullptr ){
            Carta::Lib::NdArray::TypedView<double> view( rawData, true );
            double val =  view.get( { valX, valY } );

            // set the rounded pixel value to print out
            pixelValue = QString::number(val, 'E', 3);
            //pixelValue = QString::number( val );
        }
    }
    return pixelValue;
}




int DataSource::_getFrameCount( AxisInfo::KnownType type ) const {
    int frameCount = 1;
    if ( m_image ){
        int axisIndex = Util::getAxisIndex( m_image, type );

        std::vector<int> imageShape  = m_image->dims();
        int imageDims = imageShape.size();
        if ( imageDims > axisIndex && axisIndex >= 0 ){
            frameCount = imageShape[axisIndex];
        }
    }
    return frameCount;
}



int DataSource::_getDimension( int coordIndex ) const {
    int dim = -1;
    if ( 0 <= coordIndex && coordIndex < _getDimensions()){
        dim = m_image-> dims()[coordIndex];
    }
    return dim;
}


int DataSource::_getDimensions() const {
    int imageSize = 0;
    if ( m_image ){
        imageSize = m_image->dims().size();
    }
    return imageSize;
}

std::pair<int,int> DataSource::_getDisplayDims() const {
    std::pair<int,int> displayDims(0,0);
    if ( m_image ){
        displayDims.first = m_image->dims()[m_axisIndexX];
        displayDims.second = m_image->dims()[m_axisIndexY ];
    }
    return displayDims;
}

QString DataSource::_getFileName() const {
    return m_fileName;
}

std::shared_ptr<Carta::Lib::Image::ImageInterface> DataSource::_getImage(){
    return m_image;
}

std::shared_ptr<Carta::Lib::Image::ImageInterface> DataSource::_getPermImage(){
    return m_permuteImage;
}

std::shared_ptr<Carta::Lib::PixelPipeline::CustomizablePixelPipeline> DataSource::_getPipeline() const {
    return m_pixelPipeline;
}

std::shared_ptr<Carta::Core::ImageRenderService::Service> DataSource::_getRenderer() const {
    return m_renderService;
}

// 2017/05/16    C.C. Chiang: Modify this function that it can get the intensity (pixel) for different stokes (I, Q, U and V)
std::vector<std::pair<int,double> > DataSource::_getIntensityCache( int frameLow, int frameHigh,
        const std::vector<double>& percentiles, int stokeFrame ){

    //See if it is in the cached percentiles first.
    int percentileCount = percentiles.size();
    std::vector<std::pair<int,double> > intensities(percentileCount,std::pair<int,double>(-1,0));

    //Find all the intensities we can in the cache.
    int foundCount = 0;
    for ( int i = 0; i < percentileCount; i++ ){

        std::pair<int,double> val = m_cachedPercentiles.getIntensity( frameLow, frameHigh, percentiles[i], stokeFrame);

        if ( val.first>= 0 ){

            qDebug() << "++++++++ found location and intensity in memory cache";
            intensities[i] = val;
            foundCount++;

        } else if (m_diskCache) {

            // disk cache
            // Look for the location first
            QString locationKey = QString("%1/%2/%3/%4/%5/location").arg(m_fileName).arg(frameLow).arg(frameHigh).arg(stokeFrame).arg(percentiles[i]);
            QByteArray locationVal;

            bool locationInCache = m_diskCache->readEntry(locationKey.toUtf8(), locationVal);

            qDebug() << "++++++++ location key is" << locationKey.toUtf8();

            if (locationInCache) {

                QString intensityKey = QString("%1/%2/%3/%4/%5/intensity").arg(m_fileName).arg(frameLow).arg(frameHigh).arg(stokeFrame).arg(percentiles[i]);
                QByteArray intensityVal;

                bool intensityInCache = m_diskCache->readEntry(intensityKey.toUtf8(), intensityVal);

                qDebug() << "++++++++ intensity key is" << intensityKey.toUtf8();

                if (intensityInCache) {

                    qDebug() << "++++++++ found location and intensity in disk cache";
                    intensities[i] = std::make_pair(qb2i(locationVal), qb2d(intensityVal));
                    foundCount++;

                    // put them in the memory cache
                    m_cachedPercentiles.put( frameLow, frameHigh, intensities[i].first, percentiles[i], intensities[i].second, stokeFrame);
                }
            }
        }

        qDebug() << "++++++++ For percentile" << percentiles[i]
                 << "intensity is" << intensities[i].second
                 << "and location is" << intensities[i].first << "(channel)";
    }

    //Not all percentiles were in the cache.  We are going to have to look some up.
    if ( foundCount < percentileCount ){

        qDebug() << "++++++++ Calculating some values";

        std::vector<std::pair<int,double> > allValues;

        // the SPECTRAL index is the last index of image dimension, which is corresponding to the channel-axis
        int spectralIndex = Util::getAxisIndex( m_image, AxisInfo::KnownType::SPECTRAL );
        int stokeIndex = Util::getAxisIndex( m_image, AxisInfo::KnownType::STOKES );
        qDebug() << "++++++++ Spectral Index No. is " << spectralIndex << "; Stoke Index No. is " << stokeIndex;

        // get raw data (for all stokes if any) in order to sort the raw data set by selection algorithm
        // Carta::Lib::NdArray::RawViewInterface* rawData = _getRawData( frameLow, frameHigh, spectralIndex );

        // get raw data (only for the stoke I) in order to sort the raw data set by selection algorithm
        int stokeSliceIndex = stokeFrame; // -1 is for no stoke; 0 is for stoke I; 1 is for stoke Q; 2 is for stoke U; 3 is for stoke V
        Carta::Lib::NdArray::RawViewInterface* rawData = _getRawDataForStoke( frameLow, frameHigh, spectralIndex, stokeIndex, stokeSliceIndex );
        qDebug() << "++++++++ frameLow=" << frameLow << ", frameHigh=" << frameHigh;
        qDebug() << "++++++++ set the Stoke Index for Percentile=" << stokeSliceIndex
                 << "(-1: no stoke, 0: stoke I, 1: stoke Q, 2: stoke U, 3: stoke V)";

        if ( rawData == nullptr ){
            qCritical() << "Error: could not retrieve image data to calculate missing intensities.";
            return intensities;
        }

        Carta::Lib::NdArray::TypedView<double> view( rawData, false );

        // read in all values from the view into an array
        // we need our own copy because we'll do quickselect on it...

        // Preallocate space to avoid
        // running out of memory unnecessarily through dynamic allocation
        std::vector<int> dims = rawData->dims();
        int total_size = std::accumulate(dims.begin(), dims.end(), 1, std::multiplies<int>());
        allValues.reserve(total_size);
        int index = 0;

        // filter the raw data and save in a pair array {key, value} if the value is finite
        view.forEach( [&allValues, &index] ( const double  val ) {
            if ( std::isfinite( val ) ) {
                allValues.push_back( std::make_pair(index, val) );
            }
            index++;
        }
        );
        qDebug() << "++++++++ raw data index number=" << index;

        if ( allValues.size() > 0 ){

            int divisor = total_size;
            if (spectralIndex != -1) {
                // "total_size" is the total number of data set including channels and stokes.
                // The dimension of SPECTRAL index, dims[spectralIndex], is the total number of channel.
                // So the divisor is the total number of data set for each channel (not for each stoke),
                // and each channel may contains multiple stokes.
                divisor /= dims[spectralIndex];
            }

            // return bool value: ture or false
            // following code line is to only compare the intensity values and ignore the indices
            auto compareIntensityTuples = [] (const std::pair<int,double>& lhs, const std::pair<int,double>& rhs) { return lhs.second < rhs.second; };
            auto ascendIntensityTuples  = [] (const std::pair<int,double>& lhs, const std::pair<int,double>& rhs) { return lhs.second > rhs.second; };

            for ( int i = 0; i < percentileCount; i++ ){

                //Missing intensity key value, which means we do not find it in the memory cache and the disk cache
                if ( intensities[i].first < 0 ){

                    // The definition of percentile (e.q. 99%) in the following code lines is to get elements from data array that
                    // are in the range (e.q. 0.5%-th ~ 99.5%-th of elements), note the data array has to be sorted first.
                    // Note this is not getting elements in the Gaussian distribution range (e.q. 0.5% ~ 99.5%).
                    int locationIndex = allValues.size() * percentiles[i] - 1;

                    if ( locationIndex < 0 ){
                        locationIndex = 0;
                    }

                    std::clock_t starttime = clock();
                    // // Following code line sort the partial raw data set by selection algorithm (only for array values and not for array keys).
                    // // get elements from data array that are in the range (e.q. 0.5%-th ~ 99.5%-th of elements)
                    // std::nth_element( allValues.begin(), allValues.begin()+locationIndex, allValues.end(), compareIntensityTuples );
                    std::clock_t endtime = clock();
                    // std::cout << "---------------- The time of calculating percentile by nth_element. ----------------"
                    //          << " Index : " << i << " Value : " << allValues[locationIndex].second
                    //          << " Time : " << (double)(endtime-starttime)/CLOCKS_PER_SEC << "sec" << endl;

                    starttime = clock();
                    std::vector<std::pair<int,double> >::iterator valiter;
                    double pval = allValues.begin()->second;
                    int allvalcount = 0;
                    if ( i==0 ){
                        for (valiter=allValues.begin(); valiter!=allValues.end(); valiter++){
                            if (pval > valiter->second) {
                                pval = valiter->second;
                            }
                            allvalcount++;
                        }
                        intensities[i].second = pval;
                    }
                    endtime = clock();
                    qDebug() << "---------------- The time of calculating percentile by iterator. ----------------\n"
                             << " Index : " << i << " Value : " << pval << " Count : " << allvalcount << " Size : " << total_size
                             << " Time : " << (double)(endtime-starttime)/CLOCKS_PER_SEC << "sec\n";

                    starttime = clock();
                    pval = allValues.begin()->second;
                    if ( i==1 ){
                        for (valiter=allValues.begin(); valiter!=allValues.end(); valiter++){
                            if (pval < valiter->second) {
                                pval = valiter->second;
                            }
                        }
                        intensities[i].second = pval;
                    }
                    endtime = clock();
                    qDebug() << "---------------- The time of calculating percentile by iterator. ----------------\n"
                             << " Index : " << i << " Value : " << pval
                             << " Time : " << (double)(endtime-starttime)/CLOCKS_PER_SEC << "sec\n";

                    // starttime = clock();
                    // if ( i==0 ) {
                    //     std::partial_sort (allValues.begin(), allValues.begin()+locationIndex+1, allValues.end(), compareIntensityTuples);
                    //     intensities[i].second = allValues[locationIndex].second;
                    // }
                    // pval = allValues.begin()->second;
                    // endtime = clock();
                    // std::cout << "---------------- The time of partial sorting descending. ----------------"
                    //     << " Index : " << i << " Value : " << pval
                    //     << " Time : " << (double)(endtime-starttime)/CLOCKS_PER_SEC << "sec" << endl;
                    //
                    // starttime = clock();
                    // if ( i==1 ) {
                    //     double revpercentile = 1.0 - percentiles[1];
                    //     int revlocationIndex = std::max((int)(allValues.size() * revpercentile - 1), 0);
                    //     // std::reverse(allValues.begin(), allValues.end());
                    //     std::partial_sort (allValues.begin(), allValues.begin()+revlocationIndex+1, allValues.end(), ascendIntensityTuples);
                    //     intensities[i].second = allValues[revlocationIndex].second;
                    // }
                    // pval = allValues.begin()->second;
                    // endtime = clock();
                    // std::cout << "---------------- The time of partial sorting ascending. ----------------"
                    //     << " Index : " << i << " Value : " << pval
                    //     << " Time : " << (double)(endtime-starttime)/CLOCKS_PER_SEC << "sec" << endl;

                    // get the intensity value
                    // intensities[i].second = allValues[locationIndex].second;

                    // get the channel index corresponding to the above intensity value
                    intensities[i].first = allValues[locationIndex].first / divisor;

                    if ( frameLow >= 0 ){
                        intensities[i].first += frameLow;
                    }

                    // put calculated values in both the memory cache and the disk cache
                    m_cachedPercentiles.put( frameLow, frameHigh, intensities[i].first, percentiles[i], intensities[i].second, stokeFrame);

                    if (m_diskCache) {

                        QString locationKey = QString("%1/%2/%3/%4/%5/location").arg(m_fileName).arg(frameLow).arg(frameHigh).arg(stokeFrame).arg(percentiles[i]);
                        QString intensityKey = QString("%1/%2/%3/%4/%5/intensity").arg(m_fileName).arg(frameLow).arg(frameHigh).arg(stokeFrame).arg(percentiles[i]);

                        m_diskCache->setEntry(locationKey.toUtf8(), i2qb(intensities[i].first), 0);
                        m_diskCache->setEntry(intensityKey.toUtf8(), d2qb(intensities[i].second), 0);
                    }

                    qDebug() << "++++++++ For percentile" << percentiles[i]
                             << "intensity is" << intensities[i].second
                             << "and location is" << intensities[i].first << "(channel)";
                }
            }
        }
    }
    return intensities;
}

std::vector<std::pair<int,double> > DataSource::_getIntensity( int frameLow, int frameHigh,
        const std::vector<double>& percentiles, int stokeFrame ){
    //See if we can find it in the least recently used cache; otherwise, look it up.
    std::vector<std::pair<int,double> > intensities = _getIntensityCache( frameLow,
            frameHigh, percentiles, stokeFrame );
    return intensities;
}

QColor DataSource::_getNanColor() const {
    QColor nanColor = m_renderService->getNanColor();
    return nanColor;
}

double DataSource::_getPercentile( int frameLow, int frameHigh, double intensity ) const {
    double percentile = 0;
    int spectralIndex = Util::getAxisIndex( m_image, AxisInfo::KnownType::SPECTRAL);
    Carta::Lib::NdArray::RawViewInterface* rawData = _getRawData( frameLow, frameHigh, spectralIndex );
    if ( rawData != nullptr ){
        u_int64_t totalCount = 0;
        u_int64_t countBelow = 0;
        Carta::Lib::NdArray::TypedView<double> view( rawData, false );
        view.forEach([&](const double& val) {
            if( Q_UNLIKELY( std::isnan(val))){
                return;
            }
            totalCount ++;
            if( val <= intensity){
                countBelow++;
            }
            return;
        });

        if ( totalCount > 0 ){
            percentile = double(countBelow) / totalCount;
        }
    }
    return percentile;
}

QPointF DataSource::_getPixelCoordinates( double ra, double dec, bool* valid ) const{
    QPointF result;
    CoordinateFormatterInterface::SharedPtr cf( m_image-> metaData()-> coordinateFormatter()-> clone() );
    const CoordinateFormatterInterface::VD world { ra, dec };
    CoordinateFormatterInterface::VD pixel;
    *valid = cf->toPixel( world, pixel );
    if ( *valid ){
        result = QPointF( pixel[0], pixel[1]);
    }
    return result;
}

std::pair<double,QString> DataSource::_getRestFrequency() const {
    std::pair<double,QString> restFreq( -1, "");
    if ( m_image ){
        restFreq = m_image->metaData()->getRestFrequency();
    }
    return restFreq;
}

QPointF DataSource::_getScreenPt( const QPointF& imagePt, const QPointF& pan,
        double zoom, const QSize& outputSize, bool* valid ) const {
    QPointF screenPt;
    if ( m_image != nullptr ){
        screenPt = m_renderService->image2screen( imagePt, pan, zoom, outputSize);
        *valid = true;
    }
    else {
        *valid = false;
    }
    return screenPt;
}

QPointF DataSource::_getWorldCoordinates( double pixelX, double pixelY,
        Carta::Lib::KnownSkyCS coordSys, bool* valid ) const{
    QPointF result;
    CoordinateFormatterInterface::SharedPtr cf( m_image-> metaData()-> coordinateFormatter()-> clone() );
    cf->setSkyCS( coordSys );
    int imageDims = _getDimensions();
    std::vector<double> pixel( imageDims );
    pixel[0] = pixelX;
    pixel[1] = pixelY;
    CoordinateFormatterInterface::VD world;
    *valid = cf->toWorld( pixel, world );
    if ( *valid ){
        result = QPointF( world[0], world[1]);
    }
    return result;
}

QString DataSource::_getPixelUnits() const {
    QString units = m_image->getPixelUnit().toStr();
    return units;
}

Carta::Lib::NdArray::RawViewInterface* DataSource::_getRawData( int frameStart, int frameEnd, int axisIndex ) const {
    Carta::Lib::NdArray::RawViewInterface* rawData = nullptr;
    if ( m_image ){
        // get the image dimension:
        // if the image dimension=3, then dim[0]: x-axis, dim[1]: y-axis, and dim[2]: channel-axis
        // if the image dimension=4, then dim[0]: x-axis, dim[1]: y-axis, dim[2]: stoke-axis,   and dim[3]: channel-axis
        //                                                            or  dim[2]: channel-axis, and dim[3]: stoke-axis
        int imageDim =m_image->dims().size();

        SliceND frameSlice = SliceND().next();

        for ( int i = 0; i < imageDim; i++ ){

            // only deal with the extra dimensions other than x-axis and y-axis
            if ( i != m_axisIndexX && i != m_axisIndexY ){

                // declare and set the variable "sliceSize" as the total number of channel or stoke
                int sliceSize = m_image->dims()[i];
                qDebug() << "++++++++ For the image dimension: dim[" << i << "], the total number of slices is " << sliceSize;

                SliceND& slice = frameSlice.next();

                //If it is the target axis,
                if ( i == axisIndex ){
                   //Use the passed in frame range
                   if (0 <= frameStart && frameStart < sliceSize &&
                        0 <= frameEnd && frameEnd < sliceSize ){
                        slice.start( frameStart );
                        slice.end( frameEnd + 1);
                   }
                   else {
                       slice.start(0);
                       slice.end( sliceSize);
                   }
                }
                //Or the entire range
                else {
                   slice.start( 0 );
                   slice.end( sliceSize );
                }
                slice.step( 1 );
            }
        }
        rawData = m_image->getDataSlice( frameSlice );
    }
    return rawData;
}

Carta::Lib::NdArray::RawViewInterface* DataSource::_getRawDataForStoke( int frameStart, int frameEnd, int axisIndex,
        int axisStokeIndex, int stokeSliceIndex ) const {

    Carta::Lib::NdArray::RawViewInterface* rawData = nullptr;

    if ( m_image ){
        // get the image dimension:
        // if the image dimension=3, then dim[0]: x-axis, dim[1]: y-axis, and dim[2]: channel-axis
        // if the image dimension=4, then dim[0]: x-axis, dim[1]: y-axis, dim[2]: stoke-axis, and dim[3]: channel-axis
        //                                                            or  dim[2]: channel-axis, and dim[3]: stoke-axis
        int imageDim =m_image->dims().size();

        SliceND frameSlice = SliceND().next();

        for ( int i = 0; i < imageDim; i++ ){

            // only deal with the extra dimensions other than x-axis and y-axis
            if ( i != m_axisIndexX && i != m_axisIndexY ){

                // get the number of slice (e.q. channel) in this dimension
                int sliceSize = m_image->dims()[i];

                SliceND& slice = frameSlice.next();

                // If it is the target axis..
                if ( i == axisIndex ){
                   // Use the passed in frame range
                   if ( 0 <= frameStart && frameStart < sliceSize &&
                        0 <= frameEnd && frameEnd < sliceSize ){
                       slice.start( frameStart );
                       slice.end( frameEnd + 1);
                   } else {
                       slice.start(0);
                       slice.end( sliceSize);
                   }
                } else if ( i == axisStokeIndex && stokeSliceIndex >= 0 && stokeSliceIndex <= 3){
                    // If the stoke-axis is exist (axisStokeIndex != -1),
                    // we only consider the stoke I in the first element of stoke-axis.
                    qDebug() << "++++++++ we only consider the stoke I in the first element of stoke-axis" ;
                    slice.start( stokeSliceIndex );
                    slice.end( stokeSliceIndex + 1 );
                } else {
                    // Or the entire range
                    qDebug() << "++++++++ the total number of channel is " << sliceSize;
                    slice.start( 0 );
                    slice.end( sliceSize );
                }

                slice.step( 1 );
            }
        }
        rawData = m_image->getDataSlice( frameSlice );
    }
    return rawData;
}

int DataSource::_getQuantileCacheIndex( const std::vector<int>& frames) const {
    int cacheIndex = 0;
    if ( m_image ){
        int imageSize = m_image->dims().size();
        int mult = 1;
        for ( int i = imageSize-1; i >= 0; i-- ){
            if ( i != m_axisIndexX && i != m_axisIndexY ){
                AxisInfo::KnownType axisType = _getAxisType( i );
                int frame = 0;
                if ( AxisInfo::KnownType::OTHER != axisType ){
                    int index = static_cast<int>( axisType );
                    frame = frames[index];
                }
                cacheIndex = cacheIndex + mult * frame;
                int frameCount = m_image->dims()[i];
                mult = mult * frameCount;
            }
        }
    }
    return cacheIndex;
}

std::vector<int> DataSource::_getStokeIndex( const std::vector<int>& frames ) const {
    std::vector<int> stokeIndex = {-1, -1};
    if ( m_permuteImage ) {
        int imageDim =m_permuteImage->dims().size();
        std::vector<int> indices = _getPermOrder();
        for ( int i = 0; i < imageDim; i++ ) {
            if ( i != 0 && i != 1 ) {
                int frameIndex = 0;
                int axisIndex = -1;
                int thisAxis = indices[i];
                AxisInfo::KnownType type = _getAxisType( thisAxis );
                if ( type == AxisInfo::KnownType::STOKES ) {
                    axisIndex = static_cast<int>( type );
                    frameIndex = frames[axisIndex];
                    stokeIndex[0] = axisIndex;
                    stokeIndex[1] = frameIndex;
                }
            }
        }
    }
    return stokeIndex;
}

std::vector<int> DataSource::_getChannelIndex( const std::vector<int>& frames ) const {
    std::vector<int> channelIndex = {-1, -1};
    if ( m_permuteImage ) {
        int imageDim =m_permuteImage->dims().size();
        std::vector<int> indices = _getPermOrder();
        for ( int i = 0; i < imageDim; i++ ) {
            if ( i != 0 && i != 1 ) {
                int frameIndex = 0;
                int axisIndex = -1;
                int thisAxis = indices[i];
                AxisInfo::KnownType type = _getAxisType( thisAxis );
                if ( type == AxisInfo::KnownType::SPECTRAL ) {
                    axisIndex = static_cast<int>( type );
                    frameIndex = frames[axisIndex];
                    channelIndex[0] = axisIndex;
                    channelIndex[1] = frameIndex;
                }
            }
        }
    }
    return channelIndex;
}

std::shared_ptr<Carta::Lib::Image::ImageInterface> DataSource::_getPermutedImage() const {
    std::shared_ptr<Carta::Lib::Image::ImageInterface> permuteImage(nullptr);
    if ( m_image ){
        //Build a vector showing the permute order.
        std::vector<int> indices = _getPermOrder();
        permuteImage = m_image->getPermuted( indices );
    }
    return permuteImage;
}

Carta::Lib::NdArray::RawViewInterface* DataSource::_getRawData( const std::vector<int> frames ) const {

    Carta::Lib::NdArray::RawViewInterface* rawData = nullptr;
    std::vector<int> mFrames = _fitFramesToImage( frames );

    if ( m_permuteImage ){
        int imageDim =m_permuteImage->dims().size();

        //Build a vector showing the permute order.
        std::vector<int> indices = _getPermOrder();

        SliceND nextSlice = SliceND();
        SliceND& slice = nextSlice;

        for ( int i = 0; i < imageDim; i++ ){

            //Since the image has been permuted the first two indices represent the display axes.
            if ( i != 0 && i != 1 ){

                //Take a slice at the indicated frame.
                int frameIndex = 0;
                int thisAxis = indices[i];
                AxisInfo::KnownType type = _getAxisType( thisAxis );

                // check the type of axis and its indix of slices
                int axisIndex = -1;
                if ( type == AxisInfo::KnownType::SPECTRAL ) {
                    axisIndex = static_cast<int>( type );
                    frameIndex = mFrames[axisIndex];
                    // qDebug() << "++++++++ SPECTRAL axis Index with permutation is" << axisIndex << ", the current frame Index is" << frameIndex;
                } else if ( type == AxisInfo::KnownType::STOKES ) {
                    axisIndex = static_cast<int>( type );
                    frameIndex = mFrames[axisIndex];
                    // qDebug() << "++++++++ STOKE axis Index with permutation is" << axisIndex << ", the current frame Index is" << frameIndex;
                } else if ( type != AxisInfo::KnownType::OTHER ) {
                    axisIndex = static_cast<int>( type );
                    frameIndex = mFrames[axisIndex];
                    // qDebug() << "++++++++ axis Index with permutation is" << axisIndex << ", the current frame Index is" << frameIndex;
                }

                slice.start( frameIndex );
                slice.end( frameIndex + 1);
            }

            if ( i < imageDim - 1 ){
                slice.next();
            }
        }
        rawData = m_permuteImage->getDataSlice( nextSlice );
    }
    return rawData;
}


QString DataSource::_getViewIdCurrent( const std::vector<int>& frames ) const {
   // We create an identifier consisting of the file name and -1 for the two display axes
   // and frame indices for the other axes.
   QString renderId = m_fileName;
   if ( m_image ){
       int imageSize = m_image->dims().size();
       for ( int i = 0; i < imageSize; i++ ){
           AxisInfo::KnownType axisType = _getAxisType( i );
           int axisFrame = frames[static_cast<int>(axisType)];
           QString prefix;
           //Hidden axis identified with an "f" and the index of the frame.
           if ( i != m_axisIndexX && i != m_axisIndexY ){
               prefix = "h";
           }
           //Display axis identified by a "d" plus the actual axis in the image.
           else {
               if ( i == m_axisIndexX ){
                   prefix = "dX";
               }
               else {
                   prefix = "dY";
               }
               axisFrame = i;
           }
           renderId = renderId + "//"+prefix + QString::number(axisFrame );
       }
   }
   return renderId;
}


void DataSource::_initializeSingletons( ){
    //Load the available color maps.
    if ( m_coords == nullptr ){
        m_coords = Util::findSingletonObject<CoordinateSystems>();
    }
}


bool DataSource::_isLoadable( std::vector<int> frames ) const {
        int imageDim =m_image->dims().size();
    bool loadable = true;
    for ( int i = 0; i < imageDim; i++ ){
        AxisInfo::KnownType type = _getAxisType( i );
        if ( AxisInfo::KnownType::OTHER != type ){
            int axisIndex = static_cast<int>( type );
            int frameIndex = frames[axisIndex];
                        int frameCount = m_image->dims()[i];
            if ( frameIndex >= frameCount ){
                loadable = false;
                break;
            }
        }
        else {
            loadable = false;
            break;
        }
    }
    return loadable;
}

bool DataSource::_isSpectralAxis() const {
    bool spectralAxis = false;
    int imageSize = m_image->dims().size();
    for ( int i = 0; i < imageSize; i++ ){
        AxisInfo::KnownType axisType = _getAxisType( i );
        if ( axisType == AxisInfo::KnownType::SPECTRAL ){
            spectralAxis = true;
            break;
        }
    }
    return spectralAxis;
}

void DataSource::_load(std::vector<int> frames, bool recomputeClipsOnNewFrame,
        double minClipPercentile, double maxClipPercentile){
    //Only load if the frames make sense for the image.  I.e., the frame index
    //should be less than the image size.
    if ( _isLoadable( frames ) ){
        int frameSize = frames.size();
        CARTA_ASSERT( frameSize == static_cast<int>(AxisInfo::KnownType::OTHER));
        std::vector<int> mFrames = _fitFramesToImage( frames );
        std::shared_ptr<Carta::Lib::NdArray::RawViewInterface> view ( _getRawData( mFrames ) );
        std::vector<int> dimVector = view->dims();

        //Update the clip values
        if ( recomputeClipsOnNewFrame ){
            _updateClips( view,  minClipPercentile, maxClipPercentile, mFrames );
        }
        QString cacheId=m_pixelPipeline-> cacheId();
        m_renderService-> setPixelPipeline( m_pixelPipeline,cacheId );

        QString renderId = _getViewIdCurrent( mFrames );
        m_renderService-> setInputView( view, renderId );
    }
}


void DataSource::_resetZoom(){
    m_renderService-> setZoom( ZOOM_DEFAULT );
}

void DataSource::_resetPan(){
    if ( m_permuteImage != nullptr ){
        double xCenter =  m_permuteImage-> dims()[0] / 2.0;
        double yCenter = m_permuteImage-> dims()[1] / 2.0;
        m_renderService-> setPan({ xCenter, yCenter });
    }
}

void DataSource::_resizeQuantileCache(){
    m_quantileCache.resize(0);
    int nf = 1;
    int imageSize = m_image->dims().size();
    for ( int i = 0; i < imageSize; i++ ){
        if ( i != m_axisIndexX && i != m_axisIndexY ){
            nf = nf * m_image->dims()[i];
        }
    }
    m_quantileCache.resize( nf);
}

QString DataSource::_setFileName( const QString& fileName, bool* success ){
    QString file = fileName.trimmed();
    *success = true;
    QString result;
    if (file.length() > 0) {
        if ( file != m_fileName ){
            try {
                auto res = Globals::instance()-> pluginManager()
                                      -> prepare <Carta::Lib::Hooks::LoadAstroImage>( file )
                                      .first();
                if (!res.isNull()){
                    m_image = res.val();
                    m_permuteImage = m_image;
                    // reset zoom/pan
                    _resetZoom();
                    _resetPan();

                    // clear quantile cache
                    _resizeQuantileCache();
                    m_fileName = file;
                }
                else {
                    result = "Could not find any plugin to load image";
                    qWarning() << result;
                    *success = false;
                }

            }
            catch( std::logic_error& err ){
                result = "Failed to load image "+fileName;
                qDebug() << result;
                *success = false;
            }
        }
    }
    else {
        result = "Could not load empty file.";
        *success = false;
    }
    return result;
}


void DataSource::_setColorMap( const QString& name ){
    Carta::State::ObjectManager* objManager = Carta::State::ObjectManager::objectManager();
    Carta::State::CartaObject* obj = objManager->getObject( Colormaps::CLASS_NAME );
    Colormaps* maps = dynamic_cast<Colormaps*>(obj);
    m_pixelPipeline-> setColormap( maps->getColorMap( name ) );
    m_renderService ->setPixelPipeline( m_pixelPipeline, m_pixelPipeline->cacheId());
}

void DataSource::_setColorInverted( bool inverted ){
    m_pixelPipeline-> setInvert( inverted );
    m_renderService-> setPixelPipeline( m_pixelPipeline, m_pixelPipeline-> cacheId());
}

void DataSource::_setColorReversed( bool reversed ){
    m_pixelPipeline-> setReverse( reversed );
    m_renderService-> setPixelPipeline( m_pixelPipeline, m_pixelPipeline-> cacheId());
}

void DataSource::_setColorAmounts( double newRed, double newGreen, double newBlue ){
    std::array<double,3> colorArray;
    colorArray[0] = newRed;
    colorArray[1] = newGreen;
    colorArray[2] = newBlue;
    m_pixelPipeline->setRgbMax( colorArray );
    m_renderService->setPixelPipeline( m_pixelPipeline, m_pixelPipeline->cacheId());
}

void DataSource::_setColorNan( double red, double green, double blue ){
    QColor nanColor( red, green, blue );
    m_renderService->setNanColor( nanColor );
}

bool DataSource::_setDisplayAxis( AxisInfo::KnownType axisType, int* axisIndex ){
    bool displayAxisChanged = false;
    if ( m_image ){
        int newXAxisIndex = Util::getAxisIndex(m_image, axisType);

        // invalid and let caller handle this case
        if (newXAxisIndex<0) {
            *axisIndex = newXAxisIndex;
             displayAxisChanged = true;
        }

        int imageSize = m_image->dims().size();
        if ( newXAxisIndex >= 0 && newXAxisIndex < imageSize ){
            if ( newXAxisIndex != *axisIndex ){
                *axisIndex = newXAxisIndex;
                displayAxisChanged = true;
            }
        }
    }
    return displayAxisChanged;
}

void DataSource::_setDisplayAxes(std::vector<AxisInfo::KnownType> displayAxisTypes,
        const std::vector<int>& frames ){

    int m_axisIndexX_copy = m_axisIndexX;
    int m_axisIndexY_copy = m_axisIndexY;

    int displayAxisCount = displayAxisTypes.size();
    CARTA_ASSERT( displayAxisCount == 2 );
    bool axisXChanged = false;
    bool axisYChanged = false;
    //We could have an image with two linear display axes.  In this case, we can't
    //distinguish by the type of axis as we do below.
    if ( displayAxisTypes[0] == AxisInfo::KnownType::LINEAR &&
            displayAxisTypes[1] == AxisInfo::KnownType::LINEAR ){
        if ( m_axisIndexX != 0 ){
            m_axisIndexX = 0;
            axisXChanged = true;
        }
        if ( m_axisIndexY != 1 ){
            m_axisIndexY = 1;
            axisYChanged = true;
        }
    }
    else {
        axisXChanged = _setDisplayAxis( displayAxisTypes[0], &m_axisIndexX );
        axisYChanged = _setDisplayAxis( displayAxisTypes[1], &m_axisIndexY );
    }

    // invalid displayAxisTypes
    if (m_axisIndexX == -1 || m_axisIndexY == -1 ){
        m_axisIndexX = m_axisIndexX_copy;
        m_axisIndexY = m_axisIndexY_copy;

        axisXChanged = false;
        axisYChanged = false;
    }

    if ( axisXChanged || axisYChanged ){
        m_permuteImage = _getPermutedImage();
        _resetPan();
        _resizeQuantileCache();

    }
    std::vector<int> mFrames = _fitFramesToImage( frames );
    _updateRenderedView( mFrames );
}

void DataSource::_setNanDefault( bool nanDefault ){
    m_renderService->setDefaultNan( nanDefault );
}

void DataSource::_setPan( double imgX, double imgY ){
    m_renderService-> setPan( QPointF(imgX,imgY) );
}

void DataSource::_setTransformData( const QString& name ){
    TransformsData* transformData = Util::findSingletonObject<TransformsData>();
    Carta::Lib::PixelPipeline::ScaleType scaleType = transformData->getScaleType( name );
    m_pixelPipeline->setScale( scaleType );
    m_renderService->setPixelPipeline( m_pixelPipeline, m_pixelPipeline->cacheId() );
}

void DataSource::_setZoom( double zoomAmount){
    // apply new zoom
    m_renderService-> setZoom( zoomAmount );
}


void DataSource::_setGamma( double gamma ){
    m_pixelPipeline->setGamma( gamma );
    m_renderService->setPixelPipeline( m_pixelPipeline, m_pixelPipeline->cacheId());
}

std::vector<double> DataSource::_getQuantileIntensityCache(std::shared_ptr<Carta::Lib::NdArray::RawViewInterface>& view,
        double minClipPercentile, double maxClipPercentile, const std::vector<int>& frames) {

    std::vector<int> mFrames = _fitFramesToImage( frames );
    int quantileIndex = _getQuantileCacheIndex( mFrames );
    std::vector<double> clips = m_quantileCache[ quantileIndex].m_clips;
    std::vector<int> stokeIndex = _getStokeIndex( mFrames );
    std::vector<int> channelIndex = _getChannelIndex( mFrames );

    if ( clips.size() < 2  ||
            m_quantileCache[quantileIndex].m_minPercentile != minClipPercentile  ||
            m_quantileCache[quantileIndex].m_maxPercentile != maxClipPercentile ) {

        bool minClipInCache(0);
        bool maxClipInCache(0);

        // 2017/05/23   C.C. Chiang: check if these are the right frame, stoke and percentile values
        QString minClipKey = QString("%1/%2/%3/%4/%5/intensity").arg(m_fileName).arg(channelIndex[1]).arg(channelIndex[1]).arg(stokeIndex[1]).arg(minClipPercentile);
        QString maxClipKey = QString("%1/%2/%3/%4/%5/intensity").arg(m_fileName).arg(channelIndex[1]).arg(channelIndex[1]).arg(stokeIndex[1]).arg(maxClipPercentile);
        QString minClipLocationKey = QString("%1/%2/%3/%4/%5/location").arg(m_fileName).arg(channelIndex[1]).arg(channelIndex[1]).arg(stokeIndex[1]).arg(minClipPercentile);
        QString maxClipLocationKey = QString("%1/%2/%3/%4/%5/location").arg(m_fileName).arg(channelIndex[1]).arg(channelIndex[1]).arg(stokeIndex[1]).arg(maxClipPercentile);

        qDebug() << "++++++++ minClipKey" << minClipKey.toUtf8();
        qDebug() << "++++++++ maxClipKey" << maxClipKey.toUtf8();
        qDebug() << "++++++++ minClipLocationKey" << minClipLocationKey.toUtf8();
        qDebug() << "++++++++ maxClipLocationKey" << maxClipLocationKey.toUtf8();
        qDebug() << "++++++++ Stoke Index is" << stokeIndex[1] << ", Channel Index is" << channelIndex[1];

        QByteArray minClipVal;
        QByteArray maxClipVal;

        if (m_diskCache) {
            minClipInCache = m_diskCache->readEntry(minClipKey.toUtf8(), minClipVal);
            maxClipInCache = m_diskCache->readEntry(maxClipKey.toUtf8(), maxClipVal);
        }

        if (minClipInCache && maxClipInCache) {
            clips.clear();
            clips.push_back(qb2d(minClipVal));
            clips.push_back(qb2d(maxClipVal));
            qDebug() << "++++++++ got clips from cache";
        } else {

            Carta::Lib::NdArray::Double doubleView( view.get(), false );
            clips = Carta::Core::Algorithms::quantiles2pixels(doubleView, { minClipPercentile, maxClipPercentile });

            if (m_diskCache) {
                m_diskCache->setEntry( minClipKey.toUtf8(), d2qb(clips[0]), 0);
                m_diskCache->setEntry( maxClipKey.toUtf8(), d2qb(clips[1]), 0);
                m_diskCache->setEntry( minClipLocationKey.toUtf8(), i2qb(0), 0);
                m_diskCache->setEntry( maxClipLocationKey.toUtf8(), i2qb(0), 0);

                qDebug() << "++++++++ calculated clips and put in cache";
            }
        }

        qDebug() << "++++++++ clips are" << clips[0] << "and" << clips[1];

        m_quantileCache[quantileIndex].m_clips = clips;
        m_quantileCache[quantileIndex].m_minPercentile = minClipPercentile;
        m_quantileCache[quantileIndex].m_maxPercentile = maxClipPercentile;
    }
    return clips;
}

void DataSource::_updateClips( std::shared_ptr<Carta::Lib::NdArray::RawViewInterface>& view,
        double minClipPercentile, double maxClipPercentile, const std::vector<int>& frames ){
    // get quantile intensity cache
    std::vector<double> clips = _getQuantileIntensityCache(view, minClipPercentile, maxClipPercentile, frames);
    m_pixelPipeline-> setMinMax( clips[0], clips[1] );
    m_renderService-> setPixelPipeline( m_pixelPipeline, m_pixelPipeline-> cacheId());
}

std::shared_ptr<Carta::Lib::NdArray::RawViewInterface> DataSource::_updateRenderedView( const std::vector<int>& frames ){
    // get a view of the data using the slice description and make a shared pointer out of it
    std::shared_ptr<Carta::Lib::NdArray::RawViewInterface> view( _getRawData( frames ) );
    // tell the render service to render this job
    QString renderId = _getViewIdCurrent( frames );
    m_renderService-> setInputView( view, renderId/*, m_axisIndexX, m_axisIndexY*/ );
    return view;
}

void DataSource::_viewResize( const QSize& newSize ){
    m_renderService-> setOutputSize( newSize );
}


DataSource::~DataSource() {

}
}
}
