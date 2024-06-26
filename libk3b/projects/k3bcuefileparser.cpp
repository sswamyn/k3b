/*
    SPDX-FileCopyrightText: 1998-2008 Sebastian Trueg <trueg@k3b.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "k3bcuefileparser.h"

#include "k3bmsf.h"
#include "k3bglobals.h"
#include "k3btrack.h"
#include "k3bcdtext.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>


// TODO: add method: usableByCdrecordDirectly()
// TODO: add Toc with sector sizes

class K3b::CueFileParser::Private
{
public:
    bool inFile;
    bool inTrack;
    Device::Track::TrackType trackType;
    Device::Track::DataMode trackMode;
    bool rawData;
    bool haveIndex1;
    K3b::Msf currentDataPos;
    K3b::Msf index0;

    K3b::Device::Toc toc;
    int currentParsedTrack;

    K3b::Device::CdText cdText;

    QString imageFileType;
};



K3b::CueFileParser::CueFileParser( const QString& filename )
    : K3b::ImageFileReader()
{
    d = new Private;
    openFile( filename );
}


K3b::CueFileParser::~CueFileParser()
{
    delete d;
}


void K3b::CueFileParser::readFile()
{
    setValid(true);

    d->inFile = d->inTrack = d->haveIndex1 = false;
    d->trackMode = K3b::Device::Track::UNKNOWN;
    d->toc.clear();
    d->cdText.clear();
    d->currentParsedTrack = 0;

    QFile f( filename() );
    if( f.open( QIODevice::ReadOnly ) ) {
        while( !f.atEnd() ) {
            if( !parseLine( QString::fromUtf8(f.readLine() ) ) ) {
                setValid(false);
                break;
            }
        }

        if( isValid() ) {
            // save last parsed track for which we do not have the proper length :(
            if( d->currentParsedTrack > 0 ) {
                d->toc.append( K3b::Device::Track( d->currentDataPos,
                                                 d->currentDataPos,
                                                 d->trackType,
                                                 d->trackMode ) );
            }

            // debug the toc
            qDebug() << "(K3b::CueFileParser) successfully parsed cue file." << Qt::endl
                     << "------------------------------------------------" << Qt::endl;
            for( int i = 0; i < d->toc.count(); ++i ) {
                K3b::Device::Track& track = d->toc[i];
                qDebug() << "Track " << (i+1)
                         << " (" << ( track.type() == K3b::Device::Track::TYPE_AUDIO ? "audio" : "data" ) << ") "
                         << track.firstSector().toString() << " - " << track.lastSector().toString() << Qt::endl;
            }

            qDebug() << "------------------------------------------------";
        }
    }
    else {
        qDebug() << "(K3b::CueFileParser) could not open file " << filename();
        setValid(false);
    }
}


bool K3b::CueFileParser::parseLine( QString line )
{
    // use cap(1) for the filename
    static const QRegularExpression fileRx( QRegularExpression::anchoredPattern( "FILE\\s\"?([^\"].*[^\"\\s])\"?\\s\"?(.*)\"?" ) );

    // use cap(1) for the flags
    static const QRegularExpression flagsRx( QRegularExpression::anchoredPattern( "FLAGS(\\s(DCP|4CH|PRE|SCMS)){1,4}" ) );

    // use cap(1) for the tracknumber and cap(2) for the datatype
    static const QRegularExpression trackRx( QRegularExpression::anchoredPattern( "TRACK\\s(\\d{1,2})\\s(AUDIO|CDG|MODE1/2048|MODE1/2352|MODE2/2336|MODE2/2352|CDI/2336|CDI/2352)" ) );

    // use cap(1) for the index number, cap(3) for the minutes, cap(4) for the seconds, cap(5) for the frames,
    // and cap(2) for the MSF value string
    static const QRegularExpression indexRx( QRegularExpression::anchoredPattern( "INDEX\\s(\\d{1,2})\\s((\\d+):([0-5]\\d):((?:[0-6]\\d)|(?:7[0-4])))" ) );

    // use cap(1) for the MCN
    static const QRegularExpression catalogRx( QRegularExpression::anchoredPattern( "CATALOG\\s(\\w{13,13})" ) );

    // use cap(1) for the ISRC
    static const QRegularExpression isrcRx( QRegularExpression::anchoredPattern( "ISRC\\s(\\w{5,5}\\d{7,7})" ) );

    static QString cdTextRxStr = "\"?([^\"]{0,80})\"?";

    // use cap(1) for the string
    static const QRegularExpression titleRx( QRegularExpression::anchoredPattern( "TITLE\\s" + cdTextRxStr ) );
    static const QRegularExpression performerRx( QRegularExpression::anchoredPattern( "PERFORMER\\s" + cdTextRxStr ) );
    static const QRegularExpression songwriterRx( QRegularExpression::anchoredPattern( "SONGWRITER\\s" + cdTextRxStr ) );


    // simplify all white spaces except those in filenames and CD-TEXT
    simplified( line );

    // skip comments and empty lines
    if( line.startsWith("REM") || line.startsWith('#') || line.isEmpty() )
        return true;


    //
    // FILE
    //
    if( const auto fileMatch = fileRx.match( line ); fileMatch.hasMatch() ) {

        setValid( findImageFileName( fileMatch.captured(1) ) );

        if( d->inFile ) {
            qDebug() << "(K3b::CueFileParser) only one FILE statement allowed.";
            return false;
        }
        d->inFile = true;
        d->inTrack = false;
        d->haveIndex1 = false;
        d->imageFileType = fileMatch.captured( 2 ).toLower();
        return true;
    }


    //
    // TRACK
    //
    else if( const auto trackMatch = trackRx.match( line ); trackMatch.hasMatch() ) {
        if( !d->inFile ) {
            qDebug() << "(K3b::CueFileParser) TRACK statement before FILE.";
            return false;
        }

        // check if we had index1 for the last track
        if( d->inTrack && !d->haveIndex1 ) {
            qDebug() << "(K3b::CueFileParser) TRACK without INDEX 1.";
            return false;
        }

        // save last track
        // TODO: use d->rawData in some way
        if( d->currentParsedTrack > 0 ) {
            d->toc.append( K3b::Device::Track( d->currentDataPos,
                                             d->currentDataPos,
                                             d->trackType,
                                             d->trackMode ) );
        }

        d->currentParsedTrack++;

        // parse the tracktype
        if( trackMatch.captured(2) == "AUDIO" ) {
            d->trackType = K3b::Device::Track::TYPE_AUDIO;
            d->trackMode = K3b::Device::Track::UNKNOWN;
        }
        else {
            d->trackType = K3b::Device::Track::TYPE_DATA;
            if( trackMatch.captured(2).startsWith("MODE1") ) {
                d->trackMode = K3b::Device::Track::MODE1;
                d->rawData = (trackMatch.captured(2) == "MODE1/2352");
            }
            else if( trackMatch.captured(2).startsWith("MODE2") ) {
                d->trackMode = K3b::Device::Track::MODE2;
                d->rawData = (trackMatch.captured(2) == "MODE2/2352");
            }
            else {
                qDebug() << "(K3b::CueFileParser) unsupported track type: " << trackMatch.capturedView(2);
                return false;
            }
        }

        d->haveIndex1 = false;
        d->inTrack = true;
        d->index0 = 0;

        return true;
    }


    //
    // FLAGS
    //
    else if( const auto flagsMatch = flagsRx.match( line ); flagsMatch.hasMatch() ) {
        if( !d->inTrack ) {
            qDebug() << "(K3b::CueFileParser) FLAGS statement without TRACK.";
            return false;
        }

        // TODO: save the flags
        return true;
    }


    //
    // INDEX
    //
    else if( const auto indexMatch = indexRx.match( line ); indexMatch.hasMatch() ) {
        if( !d->inTrack ) {
            qDebug() << "(K3b::CueFileParser) INDEX statement without TRACK.";
            return false;
        }

        unsigned int indexNumber = indexMatch.capturedView(1).toInt();

        K3b::Msf indexStart = K3b::Msf::fromString( indexMatch.captured(2) );

        if( indexNumber == 0 ) {
            d->index0 = indexStart;

            if( d->currentParsedTrack < 2 && indexStart > 0 ) {
                qDebug() << "(K3b::CueFileParser) first track is not allowed to have a pregap > 0.";
                return false;
            }
        }
        else if( indexNumber == 1 ) {
            d->haveIndex1 = true;
            d->currentDataPos = indexStart;
            if( d->currentParsedTrack > 1 ) {
                d->toc[d->currentParsedTrack-2].setLastSector( indexStart-1 );
                if( d->index0 > 0 && d->index0 < indexStart ) {
                    d->toc[d->currentParsedTrack-2].setIndex0( d->index0 - d->toc[d->currentParsedTrack-2].firstSector() );
                }
            }
        }
        else {
            // TODO: add index > 0
        }

        return true;
    }


    //
    // CATALOG
    //
    if( const auto catalogMatch = catalogRx.match( line ); catalogMatch.hasMatch() ) {
        // TODO: set the toc's mcn
        return true;
    }


    //
    // ISRC
    //
    if( const auto isrcMatch = isrcRx.match( line ); isrcMatch.hasMatch() ) {
        if( d->inTrack ) {
            // TODO: set the track's ISRC
            return true;
        }
        else {
            qDebug() << "(K3b::CueFileParser) ISRC without TRACK.";
            return false;
        }
    }


    //
    // CD-TEXT
    // TODO: create K3b::Device::TrackCdText entries
    //
    else if( const auto titleMatch = titleRx.match( line ); titleMatch.hasMatch() ) {
        if( d->inTrack )
            d->cdText[d->currentParsedTrack-1].setTitle( titleMatch.captured(1) );
        else
            d->cdText.setTitle( titleMatch.captured(1) );
        return true;
    }

    else if( const auto performerMatch = performerRx.match( line ); performerMatch.hasMatch() ) {
        if( d->inTrack )
            d->cdText[d->currentParsedTrack-1].setPerformer( performerMatch.captured(1) );
        else
            d->cdText.setPerformer( performerMatch.captured(1) );
        return true;
    }

    else if( const auto songwriterMatch = songwriterRx.match( line ); songwriterMatch.hasMatch() ) {
        if( d->inTrack )
            d->cdText[d->currentParsedTrack-1].setSongwriter( songwriterMatch.captured(1) );
        else
            d->cdText.setSongwriter( songwriterMatch.captured(1) );
        return true;
    }

    else {
        qDebug() << "(K3b::CueFileParser) unknown Cue line: '" << line << "'";
        return false;
    }
}


void K3b::CueFileParser::simplified( QString& s )
{
    s = s.trimmed();

    int i = 0;
    bool insideQuote = false;
    while( i < s.length() ) {
        if( !insideQuote ) {
            if( s[i].isSpace() && s[i+1].isSpace() )
                s.remove( i, 1 );
        }

        if( s[i] == '"' )
            insideQuote = !insideQuote;

        ++i;
    }
}


K3b::Device::Toc K3b::CueFileParser::toc() const
{
    return d->toc;
}


K3b::Device::CdText K3b::CueFileParser::cdText() const
{
    return d->cdText;
}


QString K3b::CueFileParser::imageFileType() const
{
    return d->imageFileType;
}


bool K3b::CueFileParser::findImageFileName( const QString& dataFile )
{
    //
    // CDRDAO does not use this image filename but replaces the extension from the cue file
    // with "bin" to get the image filename, we should take this into account
    //

    qDebug() << "(K3b::CueFileParser) trying to find file: " << dataFile;

    m_imageFilenameInCue = true;

    // first try filename as a whole (absolute)
    if( QFile::exists( dataFile ) ) {
        setImageFilename( QFileInfo(dataFile).absoluteFilePath() );
        return true;
    }

    // try the filename in the cue's directory
    if( QFileInfo( K3b::parentDir(filename()) + dataFile.section( '/', -1 ) ).isFile() ) {
        setImageFilename( K3b::parentDir(filename()) + dataFile.section( '/', -1 ) );
        qDebug() << "(K3b::CueFileParser) found image file: " << imageFilename();
        return true;
    }

    // try the filename ignoring case
    if( QFileInfo( K3b::parentDir(filename()) + dataFile.section( '/', -1 ).toLower() ).isFile() ) {
        setImageFilename( K3b::parentDir(filename()) + dataFile.section( '/', -1 ).toLower() );
        qDebug() << "(K3b::CueFileParser) found image file: " << imageFilename();
        return true;
    }

    m_imageFilenameInCue = false;

    // try removing the ending from the cue file (image.bin.cue and image.bin)
    if( QFileInfo( filename().left( filename().length()-4 ) ).isFile() ) {
        setImageFilename( filename().left( filename().length()-4 ) );
        qDebug() << "(K3b::CueFileParser) found image file: " << imageFilename();
        return true;
    }

    //
    // we did not find the image specified in the cue.
    // Search for another one having the same filename as the cue but a different extension
    //

    QDir parentDir( K3b::parentDir(filename()) );
    QString filenamePrefix = filename().section( '/', -1 );
    filenamePrefix.truncate( filenamePrefix.length() - 3 ); // remove cue extension
    qDebug() << "(K3b::CueFileParser) checking folder " << parentDir.path() << " for files: " << filenamePrefix << "*";

    //
    // we cannot use the nameFilter in QDir because of the spaces that may occur in filenames
    //
    QStringList possibleImageFiles = parentDir.entryList( QDir::Files );
    int cnt = 0;
    for( QStringList::const_iterator it = possibleImageFiles.constBegin(); it != possibleImageFiles.constEnd(); ++it ) {
        if( (*it).toLower() == dataFile.section( '/', -1 ).toLower() ||
            ((*it).startsWith( filenamePrefix ) && !(*it).endsWith( "cue" )) ) {
            ++cnt;
            setImageFilename( K3b::parentDir(filename()) + *it );
        }
    }

    //
    // we only do this if there is one unique file which fits the requirements.
    // Otherwise we cannot be certain to have the right file.
    //
    return ( cnt == 1 && QFileInfo( imageFilename() ).isFile() );
}
