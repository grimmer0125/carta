/**
 *
 **/

#include "DefaultContourGeneratorService.h"
#include "CartaLib/Algorithms/ContourConrec.h"
#include <utility>
#include <QElapsedTimer>

namespace Carta
{
namespace Core
{
DefaultContourGeneratorService::DefaultContourGeneratorService( QObject * parent )
    : Lib::IContourGeneratorService( parent )
{
    m_timer.setInterval( 1 );
    m_timer.setSingleShot( true );
    connect( & m_timer, & QTimer::timeout, this, & Me::timerCB );
}

void
DefaultContourGeneratorService::setName( const QString & name )
{
    m_name = name;
}

void
DefaultContourGeneratorService::setLevels( const std::vector < double > & levels )
{
    m_levels = levels;
}

void
DefaultContourGeneratorService::setInput( Carta::Lib::NdArray::RawViewInterface::SharedPtr rawView )
{
    m_rawView = rawView;
}

Lib::IContourGeneratorService::JobId
DefaultContourGeneratorService::start( Lib::IContourGeneratorService::JobId jobId )
{
    if ( jobId < 0 ) {
        m_lastJobId = m_lastJobId + 1;
    }
    else {
        m_lastJobId = jobId;
    }

    m_timer.start();

    return m_lastJobId;
}

void
DefaultContourGeneratorService::timerCB()
{
    // add timer
    QElapsedTimer timer;
    timer.start();

    // run the contour algorithm
    Carta::Lib::Algorithms::ContourConrec cc;

    cc.setLevels(m_levels);
    auto rawContours = cc.compute(m_rawView.get(), m_name);

    int elapsedTime = timer.elapsed();
    if (CARTA_RUNTIME_CHECKS) {
        // stop the timer and print out the elapsed time
        qDebug() << "++++++++ [contour] Spending time for calculating contours:" << elapsedTime << "ms";
    }

    // build the result
    Result result;
    for( size_t i = 0 ; i < m_levels.size() ; ++ i) {
        Carta::Lib::Contour contour( m_levels[i], rawContours[i]);
        result.add( contour);
    }

    emit done( result, m_lastJobId );
}
}
}
