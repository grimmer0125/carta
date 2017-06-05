/**
 *
 **/

#include "CCCoordinateFormatter.h"
#include <casacore/coordinates/Coordinates.h>
#include <casacore/measures/Measures/Stokes.h>
#include <QDebug>

#ifdef DONT_COMPILE
#define CARTA_DEBUG_THIS_FILE 0

#if CARTA_DEBUG_THIS_FILE > 0 && defined ( CARTA_DEBUG_OUTPUT )
#define cartaDebug qDebug
#else
#define cartaDebug while ( false ) \
        qDebug
#endif
#endif

/// shortcut to HtmlString
typedef Carta::Lib::HtmlString HtmlString;

class DoubleFormatter
{
    CLASS_BOILERPLATE( DoubleFormatter );

public:

    Me
    showPlus( bool flag )
    {
        m_showPlus = flag;
        return * this;
    }

    Me
    sexagesimal( bool flag, QString separator = ":" )
    {
        m_sexagesimal = flag;
        m_separator = separator;
        return * this;
    }

    Me
    precision( int p )
    {
        m_precision = p;
        return * this;
    }

    QString
    go( double value )
    {
        QString result;
        if ( value > 0 && m_showPlus ) {
            result += "+";
        }
        if ( value < 0 ) {
            value = - value;
            result += "-";
        }
        if ( ! m_sexagesimal ) {
            if ( m_precision < 0 ) {
                result += QString::number( value, 'e', - m_precision );
            }
            else {
                result += QString::number( value, 'f', m_precision );
            }
        }
        else {
            // format the 60^2 part and remove it from value
            long dig;
            dig = long (value) / ( 60 * 60 );
            result += QString::number( dig ) + ':';
            value -= dig * 60 * 60;

            // format the 60^1 part and remove it from value
            // but make sure to insert a leading 0
            dig = long (value) / 60;
            if ( dig < 10 ) { result += '0'; } result += QString::number( dig ) + ':';
            value -= dig * 60;

            // format the last part 60^0 with the requested precision
            // note: negative precision is converted to positive
            // note: we add 100 and then remove first character (the '1') to get
            //       leading 0 if any...
            result += QString::number( value + 100, 'f', abs( m_precision ) ).remove( 0, 1 );
        }
        return result;
    } // go

protected:

    bool m_showPlus = false;
    bool m_sexagesimal = false;
    int m_precision = 3;
    QString m_separator = ":";
};

static SkyFormatting
getDefaultForSkyCS( const Carta::Lib::KnownSkyCS & skyCS )
{
    switch ( skyCS )
    {
    case Carta::Lib::KnownSkyCS::B1950 :
    case Carta::Lib::KnownSkyCS::J2000 :
    case Carta::Lib::KnownSkyCS::ICRS :
        return SkyFormatting::Sexagesimal;

    default :
        return SkyFormatting::Degrees;
    }
}

CCCoordinateFormatter::CCCoordinateFormatter( std::shared_ptr < casa::CoordinateSystem > casaCS )
    :  m_displayAxes( 2, Carta::Lib::AxisInfo::KnownType::OTHER )
{
    // remember the pointer to casa coordinate systems
    m_casaCS = casaCS;

    // prepare the axis info
    parseCasaCS();
}

CCCoordinateFormatter *
CCCoordinateFormatter::clone() const
{
    CCCoordinateFormatter * res = new CCCoordinateFormatter( * this );
    res->m_casaCS.reset( new casa::CoordinateSystem( * m_casaCS ) );
    return res;
}

int
CCCoordinateFormatter::nAxes() const
{
    CARTA_ASSERT( m_casaCS );
    return m_casaCS->nPixelAxes();
}

QStringList
CCCoordinateFormatter::formatFromPixelCoordinate( const CoordinateFormatterInterface::VD & pix )
{
    // first convert the pixel coordinates to world coordinates
    casa::Vector < casa::Double > world;
    casa::Vector < casa::Double > pixel = pix;
    m_casaCS->toWorld( world, pix );

    casa::String Type;
    // for spectral axis
    // convert freq to radio velocity
    int NumberofSpectralAxis = -1;
    casa::Quantum<casa::Double> velocity;
    if(m_casaCS->hasSpectralAxis())
    {
        NumberofSpectralAxis = m_casaCS->spectralAxisNumber();
        casa::SpectralCoordinate casaSpeSystem = m_casaCS->spectralCoordinate();
        // bool test = casaSpeSystem.setNativeType(casa::SpectralCoordinate::SpecType::VOPT);
        // casa::SpectralCoordinate::specTypetoString(Type, casaSpeSystem.nativeType());
        // casaSpeSystem.setVelocity(casa::String("km/s"), casa::MDoppler::OPTICAL);
        casaSpeSystem.pixelToVelocity(velocity,pixel(NumberofSpectralAxis));
    }


    // format each axis
//    QStringList list;
//    for (unsigned int i = 0; i < m_casaCS->nCoordinates(); i++) {
//        casa::String units;
//        casa::String s = m_casaCS->format(units, casa::Coordinate::FIXED, world[i], i);
//        list.append(QString("%1%2").arg(s.c_str()).arg(units.c_str()));
//    }
//    return list;

    QStringList list;
    for ( int i = 0 ; i < nAxes() ; i++ ) {
        QString val = formatWorldValue( i, world[i] );
        // This part should be integrated to the formatWorldValue()
        if(NumberofSpectralAxis == i ){
            switch ( specCS() ) {
               case KnownSpecCS::FREQ :
                   break;
               case KnownSpecCS::VRAD :
                   val = QString::number(casa::Double(velocity.getValue()))
                       + " " + velocity.getUnit().c_str();
                   break;
               case KnownSpecCS::VOPT :
                   val = QString::number(casa::Double(velocity.getValue()))
                       + " " + velocity.getUnit().c_str();
                   break;
               case KnownSpecCS::BETA :
                   break;
               case KnownSpecCS::WAVE :
                   break;
               case KnownSpecCS::AWAV :
                   break;

               default:
                   break;
            }
        }
        list.append( val );
    }
    return list;
} // formatFromPixelCoordinate

QString
CCCoordinateFormatter::calculateFormatDistance( const CoordinateFormatterInterface::VD & p1,
                                                const CoordinateFormatterInterface::VD & p2 )
{
    Q_UNUSED( p1 );
    Q_UNUSED( p2 );

    qFatal( "not implemented" );
}

int
CCCoordinateFormatter::axisPrecision( int axis )
{
    CARTA_ASSERT( axis >= 0 && axis < nAxes() );
    return m_precisions[axis];
}

CCCoordinateFormatter::Me &
CCCoordinateFormatter::setAxisPrecision( int precision, int axis )
{
    CARTA_ASSERT( axis >= 0 && axis < nAxes() );
    m_precisions[axis] = precision;
    return * this;
}

bool
CCCoordinateFormatter::toWorld( const CoordinateFormatterInterface::VD & pixel,
                                CoordinateFormatterInterface::VD & world ) const
{
    casa::Vector< casa::Double > worldD = world;
    casa::Vector< casa::Double > pixelD = pixel;
    bool valid = m_casaCS->toWorld( worldD, pixelD );
    world = {worldD[0], worldD[1] };
    return valid;
}

bool
CCCoordinateFormatter::toPixel( const CoordinateFormatterInterface::VD & world,
                                CoordinateFormatterInterface::VD & pixel ) const
{
    casa::Vector < casa::Double > worldD = world;
    casa::Vector < casa::Double > pixelD = pixel;
    bool valid = m_casaCS->toPixel( pixelD, worldD );
    pixel = { pixelD[0], pixelD[1] };
    return valid;
}

void
CCCoordinateFormatter::setTextOutputFormat( CoordinateFormatterInterface::TextFormat fmt )
{
    m_textOutputFormat = fmt;
}

const Carta::Lib::AxisInfo &
CCCoordinateFormatter::axisInfo( int ind ) const
{
    CARTA_ASSERT( ind >= 0 && ind < nAxes() );
    return m_axisInfos[ind];
}

CCCoordinateFormatter::Me &
CCCoordinateFormatter::disableAxis( int ind )
{
    Q_UNUSED( ind );
    qFatal( "not implemented" );
}

CCCoordinateFormatter::Me &
CCCoordinateFormatter::enableAxis( int ind )
{
    Q_UNUSED( ind );
    qFatal( "not implemented" );
}

Carta::Lib::KnownSkyCS
CCCoordinateFormatter::skyCS()
{
    if ( ! m_casaCS->hasDirectionCoordinate() ) {
        return KnownSkyCS::Unknown;
    }
    int which = m_casaCS->directionCoordinateNumber();
    const casa::DirectionCoordinate & dirCoord = m_casaCS->directionCoordinate( which );
    casa::MDirection::Types dirType = dirCoord.directionType( true );
    switch ( dirType )
    {
    case casa::MDirection::Types::B1950 :
        return KnownSkyCS::B1950;

    case casa::MDirection::Types::J2000 :
        return KnownSkyCS::J2000;

    case casa::MDirection::Types::ICRS :
        return KnownSkyCS::ICRS;

    case casa::MDirection::Types::GALACTIC :
        return KnownSkyCS::Galactic;

    case casa::MDirection::Types::ECLIPTIC :
        return KnownSkyCS::Ecliptic;

    default :
        return KnownSkyCS::Unknown;
    } // switch
} // skyCS

Carta::Lib::KnownSpecCS
CCCoordinateFormatter::specCS(){
    if ( ! m_casaCS->hasSpectralAxis() ) {
        return KnownSpecCS::Unknown;
    }
    const casa::SpectralCoordinate & specCoord= m_casaCS->spectralCoordinate();
    casa::SpectralCoordinate::SpecType specType = specCoord.nativeType();
    switch (specType) {
        case casa::SpectralCoordinate::SpecType::FREQ :
            return KnownSpecCS::FREQ;
        case casa::SpectralCoordinate::SpecType::VRAD :
            return KnownSpecCS::VRAD;
        case casa::SpectralCoordinate::SpecType::VOPT :
            return KnownSpecCS::VOPT;
        case casa::SpectralCoordinate::SpecType::BETA :
            return KnownSpecCS::BETA;
        case casa::SpectralCoordinate::SpecType::WAVE :
            return KnownSpecCS::WAVE;
        case casa::SpectralCoordinate::SpecType::AWAV :
            return KnownSpecCS::AWAV;

        default:
            return KnownSpecCS::Unknown;
    }
}

CCCoordinateFormatter::Me &
CCCoordinateFormatter::setSkyCS( const KnownSkyCS & scs )
{
    //qDebug() << "setSkyCS" << static_cast < int > ( scs );

    // don't even try to set this to unknown
    if ( scs == KnownSkyCS::Unknown ) {
        return * this;
    }

    // find out where the direction world coordinate lives
    int which = m_casaCS->directionCoordinateNumber();
    if ( which < 0 ) {
        // this system does not have sky cs, so we are done
        return * this;

        // find out which axes correspond to the world coordinate array(longitude/latitude)
    }
    auto pixelAxes = m_casaCS->directionAxesNumbers();
    CARTA_ASSERT( pixelAxes.size() == 2 );
    CARTA_ASSERT( 0 <= pixelAxes[0] && pixelAxes[0] < nAxes() );
    CARTA_ASSERT( 0 <= pixelAxes[1] && pixelAxes[1] < nAxes() );

    // make a copy of it
    casa::DirectionCoordinate dirCoordCopy =
        casa::DirectionCoordinate( m_casaCS->directionCoordinate( which ) );

    // change the system in the copy
    casa::MDirection::Types mdir;
    switch ( scs )
    {
    case KnownSkyCS::B1950 :
        mdir = casa::MDirection::B1950;
        break;
    case KnownSkyCS::J2000 :
        mdir = casa::MDirection::J2000;
        break;
    case KnownSkyCS::ICRS :
        mdir = casa::MDirection::ICRS;
        break;
    case KnownSkyCS::Ecliptic :
        mdir = casa::MDirection::ECLIPTIC;
        break;
    case KnownSkyCS::Galactic :
        mdir = casa::MDirection::GALACTIC;
        break;
    default :
        CARTA_ASSERT_ALWAYS_X( false, "Internal error" );
        break;
    } // switch
    dirCoordCopy.setReferenceConversion( mdir );
    if ( ! m_casaCS->replaceCoordinate( dirCoordCopy, which ) ) {
        qWarning() << "Could not set wcs because replaceCoordinate() failed";
        return * this;
    }

    // now we need to adjust axisinfos, formatting and precision
    setSkyFormatting( SkyFormatting::Default );
    parseCasaCSi( pixelAxes[0] );
    parseCasaCSi( pixelAxes[1] );

    // chaning support
    return * this;
} // setSkyCS

CCCoordinateFormatter::Me &
CCCoordinateFormatter::setSpecCS( const KnownSpecCS & spcs )
{
    // don't even try to set this to unknown
    if ( spcs == KnownSpecCS::Unknown ) {
        return * this;
    }

    // find out where the direction world coordinate lives
    // int which = m_casaCS->spectralAxisNumber();
    int which = m_casaCS->spectralCoordinateNumber();
    if ( which < 0 ) {
        // this system does not have spec cs, so we are done
        return * this;
    }

    casa::SpectralCoordinate specCSCopy = m_casaCS->spectralCoordinate();
    // change the system in the copy
    casa::SpectralCoordinate::SpecType sptype;
    switch (spcs) {
        case KnownSpecCS::FREQ :
            sptype = casa::SpectralCoordinate::SpecType::FREQ;
            break;
        case KnownSpecCS::VRAD :
            sptype = casa::SpectralCoordinate::SpecType::VRAD;
            specCSCopy.setVelocity(casa::String("km/s"), casa::MDoppler::RADIO);
            break;
        case KnownSpecCS::VOPT :
            sptype = casa::SpectralCoordinate::SpecType::VOPT;
            specCSCopy.setVelocity(casa::String("m/s"), casa::MDoppler::OPTICAL);
            break;
        case KnownSpecCS::BETA :
            sptype = casa::SpectralCoordinate::SpecType::BETA;
            break;
        case KnownSpecCS::WAVE :
            sptype = casa::SpectralCoordinate::SpecType::WAVE;
            break;
        case KnownSpecCS::AWAV :
            sptype = casa::SpectralCoordinate::SpecType::AWAV;
            break;

        default:
            CARTA_ASSERT( false );
            break;
    }

    if ( !specCSCopy.setNativeType(sptype) ){
        qWarning() << "Failed to set the type of spectral coordinate.";
        return * this;
    }

    if ( ! m_casaCS->replaceCoordinate( specCSCopy, which ) ) {
        qWarning() << "Could not set wcs because replaceCoordinate() failed";
        return * this;
    }
    // now we need to adjust axisinfos, formatting and precision
    // TODO: set the format of spectral coordinate, such as the unit and rest freq, etc.
    // setSkyFormatting( SkyFormatting::Default ); // left for reminding

    parseCasaCSi( m_casaCS->spectralAxisNumber() );

    // chaning support
    return * this;
}

SkyFormatting
CCCoordinateFormatter::skyFormatting()
{
    return m_skyFormatting;
}

CCCoordinateFormatter::Me &
CCCoordinateFormatter::setSkyFormatting( SkyFormatting format )
{
    m_skyFormatting = format;
    if ( m_skyFormatting == SkyFormatting::Default ) {
        m_skyFormatting = getDefaultForSkyCS( skyCS() );
    }
    return * this;
}

// based on information in m_casaCS:
// - extract axis infos
// - set default precisions
// - set default sky formatting
void
CCCoordinateFormatter::parseCasaCS()
{
    /*qDebug() << "CCC nAxes=" << nAxes();
    for ( auto & u : m_casaCS->worldAxisUnits() ) {
        qDebug() << "all units:" << u.c_str();
    }
    for ( auto & u : m_casaCS->worldAxisNames() ) {
        qDebug() << "all names:" << u.c_str();
    }*/

    // default precision is 3
    m_precisions.resize( nAxes(), 3 );
    m_axisInfos.resize( nAxes() );

    for ( int i = 0 ; i < nAxes() ; i++ ) {
        parseCasaCSi( i );
    }
    /*qDebug() << "Parsed axis infos:";
    for ( auto & ai : m_axisInfos ) {
        qDebug() << "  lp:" << ai.longLabel().plain() << "lh:" << ai.longLabel().html()
                 << "sp:" << ai.shortLabel().html() << "sh:" << ai.shortLabel().html()
                 << "u:" << ai.unit();
    }*/

    // set formatting to default
    setSkyFormatting( SkyFormatting::Default );
} // parseCasaCS

void
CCCoordinateFormatter::parseCasaCSi( int pixelAxis )
{
    CARTA_ASSERT( 0 <= pixelAxis && pixelAxis < nAxes() );

    // find the pixel axes in casacore's coordinate system
    // coord will be the index of the 'coordinate'
    // and coord2 will be an index within that index...
    // warning: casa's coordinates and axes are two completely different things!
    // e.g. a standard 4D fits file with frequency and stokes has 3 coordinates, but
    // 4 axes...
    int coord; // this is the world coordinate
    int coord2; // this is the index within world coordinate (0 for all but latitude)

    m_casaCS->findPixelAxis( coord, coord2, pixelAxis );

    //qDebug() << pixelAxis << "-->" << coord << "," << coord2;
    //qDebug() << "   "
    //         << casa::Coordinate::typeToString( m_casaCS->coordinate( coord ).type() ).c_str();

    AxisInfo & aInfo = m_axisInfos[pixelAxis];

    // default will be unknown axis
    aInfo.setKnownType( AxisInfo::KnownType::OTHER )
        .setLongLabel( HtmlString::fromPlain( "Unknown" ) )
        .setShortLabel( HtmlString::fromPlain( "Unknown" ) )
        .setUnit( "unknown" );

    // did we find the world coordinate for this axis in casa core's coordinatesystem?
    if ( coord >= 0 ) {
        const auto & cc = m_casaCS->coordinate( coord );
        auto skycs = skyCS();
        KnownSpecCS speccs = specCS();

        // Directly save the label from casa
        QString rawAxisLabel = cc.worldAxisNames() ( coord2 ).c_str();
        QString longLabel = rawAxisLabel.toLower();
        // Transform each first character to uppercase.
        if ( rawAxisLabel != "" ){
            QStringList longLabelSplit = longLabel.split(" ");
            for( int i=0; i<longLabelSplit.length(); i++){
                longLabelSplit[i].replace(0, 1, longLabelSplit[i].at(0).toUpper());
            }
            longLabel = longLabelSplit.join(" ");
        }
        aInfo.setLongLabel( HtmlString::fromPlain( longLabel ) );

        // we handle sky coordinate
        if ( cc.type() == casa::Coordinate::DIRECTION ) {
            // is it longitude?
            if ( coord2 == 0 ) {
                aInfo.setKnownType( AxisInfo::KnownType::DIRECTION_LON );

                // B1950,J200 and ICRS share labels
                if ( skycs == KnownSkyCS::B1950 ||
                     skycs == KnownSkyCS::J2000 ||
                     skycs == KnownSkyCS::ICRS ) {
                    aInfo.setShortLabel( HtmlString( "RA", "&alpha;" ) );
                    //precision to 0.001 arcsec
                    m_precisions[pixelAxis] = 5;
                }
                else if ( skycs == KnownSkyCS::Ecliptic ) {
                    aInfo.setShortLabel( HtmlString( "ELon", "&lambda;"));
                        //.setShortLabel( HtmlString( "ELon", "l" ) );
                    //precision to 0.001 arcsec
                    m_precisions[pixelAxis] = 7;
                }
                else if ( skycs == KnownSkyCS::Galactic ) {
                    aInfo.setShortLabel( HtmlString( "GLon", "l"));
                        //.setShortLabel( HtmlString( "GLon", "&lambda;" ) );
                    //precision to 0.001 arcsec
                    m_precisions[pixelAxis] = 7;
                }
                else {
                    CARTA_ASSERT( false );
                }
            }
            // it's latitude then
            else {
                aInfo.setKnownType( AxisInfo::KnownType::DIRECTION_LAT );

                // B1950,J200 and ICRS share labels
                if ( skycs == KnownSkyCS::B1950 ||
                     skycs == KnownSkyCS::J2000 ||
                     skycs == KnownSkyCS::ICRS ) {
                    aInfo.setShortLabel( HtmlString( "Dec", "&delta;" ) );
                    //precision to 0.001 arcsec
                    m_precisions[pixelAxis] = 4;
                }
                else if ( skycs == KnownSkyCS::Ecliptic ) {
                    aInfo.setShortLabel( HtmlString( "Elat", "&beta;"));
                        //.setShortLabel( HtmlString( "ELat", "b" ) );
                    //precision to 0.001 arcsec
                    m_precisions[pixelAxis] = 7;
                }
                else if ( skycs == KnownSkyCS::Galactic ) {
                    aInfo.setShortLabel( HtmlString( "GLat", "b"));
                        //.setShortLabel( HtmlString( "GLat", "&beta;" ) );
                    //precision to 0.001 arcsec
                    m_precisions[pixelAxis] = 7;
                }
                else {
                    CARTA_ASSERT( false );
                }
            }
        }
        else if ( cc.type() == casa::Coordinate::SPECTRAL ) {
            aInfo.setKnownType( AxisInfo::KnownType::SPECTRAL );

            switch (speccs) {
                case KnownSpecCS::FREQ:
                    aInfo.setShortLabel( HtmlString( "Freq", "Freq") );
                    break;
                case KnownSpecCS::VRAD:
                    aInfo.setShortLabel( HtmlString( "Vrad", "Vrad") );
                    break;
                case KnownSpecCS::VOPT:
                    aInfo.setShortLabel( HtmlString( "Vopt", "Vopt") );
                    break;
                case KnownSpecCS::BETA:
                    aInfo.setShortLabel( HtmlString( "Beta", "Beta") );
                    break;
                case KnownSpecCS::WAVE:
                    aInfo.setShortLabel( HtmlString( "Wave", "Wave") );
                    break;
                case KnownSpecCS::AWAV:
                    aInfo.setShortLabel( HtmlString( "Awav", "Awav") );
                    break;
                case KnownSpecCS::Unknown:
                    aInfo.setShortLabel( HtmlString( "Unknown", "Unknown") );
                    break;

                default:
                    CARTA_ASSERT( false );
            }
                // .setLongLabel( HtmlString::fromPlain("Radio Velocity") )
                // .setShortLabel( HtmlString::fromPlain( longLabel ));
                // .setShortLabel( HtmlString( "Vrad", "Vrad") );
                // .setShortLabel( HtmlString( "Freq", "Freq" ) );

            m_precisions[pixelAxis] = 9;
        }
        else if ( cc.type() == casa::Coordinate::STOKES ) {
            aInfo.setKnownType( AxisInfo::KnownType::STOKES )
                .setShortLabel( HtmlString::fromPlain( "Stokes" ) );
        }
        else if ( cc.type() == casa::Coordinate::TABULAR ) {
            aInfo.setKnownType( AxisInfo::KnownType::TABULAR );

            //            else if ( cc.type() == casa::Coordinate::QUALITY ) {
            //                aInfo.setKnownType( aInfo.KnownType::QUALITY);
            //            }
        }
        else if ( cc.type() == casa::Coordinate::LINEAR ){
            aInfo.setKnownType( AxisInfo::KnownType::LINEAR )
                .setShortLabel( HtmlString::fromPlain( rawAxisLabel.toLower() ) );
        }
        else {
            // other types... we copy whatever casacore dishes out
            aInfo.setKnownType( AxisInfo::KnownType::OTHER );
            // QString rawAxisLabel = cc.worldAxisNames() ( coord2 ).c_str();
            QString shortLabel = rawAxisLabel;
            // aInfo.setLongLabel( HtmlString::fromPlain( rawAxisLabel ) );
            aInfo.setShortLabel( HtmlString::fromPlain( shortLabel ) );
        }
        CARTA_ASSERT( cc.worldAxisNames().size() > 0 );

        // we always take the unit from casa
        aInfo.setUnit( cc.worldAxisUnits() ( coord2 ).c_str() );
    }
    else {
        // this should never happen that casacore didn't find world coordinates for
        // the given axis... but let's not panic and just leave it a default value
    }
} // parseCasaCSi

QString
CCCoordinateFormatter::formatWorldValue( int whichAxis, double worldValue )
{
    // get info for this axis
    const AxisInfo & ai = axisInfo( whichAxis );

    //
    // decide what to do based on the type of the axis
    //

    // for longigute / latitude we do the same thing, except for a different factor
    // when doing sexagesimal
    if ( ai.knownType() == AxisInfo::KnownType::DIRECTION_LON
         || ai.knownType() == AxisInfo::KnownType::DIRECTION_LAT ) {
        double sexFactor = ( ai.knownType() == AxisInfo::KnownType::DIRECTION_LON )
                           ? 24 * 60 * 60 / ( 2 * M_PI )
                           : 180 * 60 * 60 / M_PI;

        // for longitude values, wrap around negative values
        if ( ai.knownType() == AxisInfo::KnownType::DIRECTION_LON && worldValue < 0 ) {
            worldValue += 2 * M_PI;
        }
        if ( skyFormatting() == SkyFormatting::Radians ) {
            return DoubleFormatter()
                       .showPlus( false )
                       .sexagesimal( false )
                       .precision( axisPrecision( whichAxis ) )
                       .go( worldValue );
        }
        if ( skyFormatting() == SkyFormatting::Degrees ) {
            return DoubleFormatter()
                       .showPlus( true )
                       .sexagesimal( false )
                       .precision( axisPrecision( whichAxis ) )
                       .go( worldValue * 180 / M_PI )
                   + ( m_textOutputFormat == TextFormat::Html ? "&deg;" : "deg" );
        }
        CARTA_ASSERT( skyFormatting() == SkyFormatting::Sexagesimal );
        return DoubleFormatter()
                   .showPlus( true )
                   .sexagesimal( true, ":" )
                   .precision( axisPrecision( whichAxis ) )
                   .go( worldValue * sexFactor )

//                + QString("(%1)").arg(worldValue*180/M_PI,0,'f',10)
        ;
    }

    // for stokes we convert to a string using casacore's Stokes class
    else if ( ai.knownType() == AxisInfo::KnownType::STOKES ) {
        return casa::Stokes::name( static_cast < casa::Stokes::StokesTypes > ( round( worldValue ) ) )
                   .c_str();
    }
    else if ( ai.knownType() == AxisInfo::KnownType::SPECTRAL ){
        int exp = 1;
        QStringList availUnits={"Hz","KHz","MHz","GHz"};
        int unitCount = availUnits.size();
        for ( ; exp < unitCount; exp++ ){
            if ( worldValue < pow(10, 3*exp) ){
                break;
            }
        }
        exp = exp - 1;
        QString oldUnit = ai.unit();
        int diff = 0;
        QString unit = oldUnit;
        if ( exp >= 1 ){
          for ( int i = 0; i < availUnits.size(); i++  ){
              if ( availUnits[i] == oldUnit ){
                  if ( i < exp ){
                      diff = exp - i;
                      break;
                  }
              }
          }
        }
        unit = availUnits[exp];
        worldValue = worldValue / pow(10,3*diff);
        int precision = axisPrecision( whichAxis);
        return QString::number(worldValue, 'g', precision) +" "+ unit;
    }

    // for other types we do verbatim formatting
    QString unit = ai.unit();
    if ( m_textOutputFormat == TextFormat::Html ) {
        unit = unit.toHtmlEscaped();
    }
    return DoubleFormatter()
               .showPlus( false )
               .sexagesimal( false )
               .precision( axisPrecision( whichAxis ) )
               .go( worldValue ) + unit;
} // formatWorldValue
