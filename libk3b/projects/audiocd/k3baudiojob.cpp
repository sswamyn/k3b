/*
 *
 * $Id$
 * Copyright (C) 2003 Sebastian Trueg <trueg@k3b.org>
 *
 * This file is part of the K3b project.
 * Copyright (C) 1998-2004 Sebastian Trueg <trueg@k3b.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * See the file "COPYING" for the exact licensing terms.
 */


#include "k3baudiojob.h"

#include <k3baudioimager.h>
#include <k3baudiodoc.h>
#include <k3baudiotrack.h>
#include <k3baudiodatasource.h>
#include <k3baudionormalizejob.h>
#include "k3baudiojobtempdata.h"
#include <k3bdevicemanager.h>
#include <k3bdevicehandler.h>
#include <k3bdevice.h>
#include <k3bcdtext.h>
#include <k3bmsf.h>
#include <k3bglobals.h>
#include <k3bexternalbinmanager.h>
#include <k3bcore.h>
#include <k3bcdrecordwriter.h>
#include <k3bcdrdaowriter.h>
#include <k3bexceptions.h>
#include <k3btocfilewriter.h>
#include <k3binffilewriter.h>

#include <qfile.h>
#include <qvaluevector.h>

#include <kdebug.h>
#include <klocale.h>
#include <ktempfile.h>


class K3bAudioJob::Private
{
  public:
  Private()
    : copies(1),
      copiesDone(0) {
  }

  int copies;
  int copiesDone;
};


K3bAudioJob::K3bAudioJob( K3bAudioDoc* doc, K3bJobHandler* hdl, QObject* parent )
  : K3bBurnJob( hdl, parent ),
    m_doc( doc ),
    m_normalizeJob(0)
{
  d = new Private;

  m_audioImager = new K3bAudioImager( m_doc, this, this );
  connect( m_audioImager, SIGNAL(infoMessage(const QString&, int)), 
	   this, SIGNAL(infoMessage(const QString&, int)) );
  connect( m_audioImager, SIGNAL(percent(int)), 
	   this, SLOT(slotAudioDecoderPercent(int)) );
  connect( m_audioImager, SIGNAL(subPercent(int)), 
	   this, SLOT(slotAudioDecoderSubPercent(int)) );
  connect( m_audioImager, SIGNAL(finished(bool)), 
	   this, SLOT(slotAudioDecoderFinished(bool)) );
  connect( m_audioImager, SIGNAL(nextTrack(int, int)), 
	   this, SLOT(slotAudioDecoderNextTrack(int, int)) );

  m_writer = 0;
  m_tempData = new K3bAudioJobTempData( m_doc, this );
}


K3bAudioJob::~K3bAudioJob()
{
  delete d;
}


K3bDevice::Device* K3bAudioJob::writer() const
{
  return m_doc->burner();
}


K3bDoc* K3bAudioJob::doc() const
{
  return m_doc;
}


void K3bAudioJob::start()
{
  emit started();

  m_written = true;
  m_canceled = false;
  m_errorOccuredAndAlreadyReported = false;
  d->copies = m_doc->copies();
  d->copiesDone = 0;

  if( m_doc->dummy() )
    d->copies = 1;

  emit newTask( i18n("Preparing data") );

  // we don't need this when only creating image and it is possible
  // that the burn device is null
  if( !m_doc->onlyCreateImages() ) {
    // determine writing mode
    if( m_doc->writingMode() == K3b::WRITING_MODE_AUTO ) {
      //
      // DAO is always the first choice
      // choose TAO if the user wants to use cdrecord since
      // there are none-DAO writers that are supported by cdrdao
      //
      // There are some writers that fail to create proper audio cds
      // in DAO mode. For those we choose the raw writing mode.
      //
      if( !writer()->dao() && writingApp() == K3b::CDRECORD )
	m_usedWritingMode = K3b::TAO;
      else if( K3bExceptions::brokenDaoAudio( writer() ) )
	m_usedWritingMode = K3b::RAW;
      else
	m_usedWritingMode = K3b::DAO;
    }
    else
      m_usedWritingMode = m_doc->writingMode();

    bool cdrecordOnTheFly = false;
    bool cdrecordCdText = false;
    if( k3bcore->externalBinManager()->binObject("cdrecord") ) {
      cdrecordOnTheFly = k3bcore->externalBinManager()->binObject("cdrecord")->version >= K3bVersion( 2, 1, -1, "a13" );
      cdrecordCdText = k3bcore->externalBinManager()->binObject("cdrecord")->hasFeature( "cdtext" );
    }    

    // determine writing app
    if( writingApp() == K3b::DEFAULT ) {
      if( m_usedWritingMode == K3b::DAO ) {
	// there are none-DAO writers that are supported by cdrdao
	if( !writer()->dao() ||
	    ( !cdrecordOnTheFly && m_doc->onTheFly() ) ||
	    ( m_doc->cdText() && !cdrecordCdText ) ||
	    m_doc->hideFirstTrack() )
	  m_usedWritingApp = K3b::CDRDAO;
	else
	  m_usedWritingApp = K3b::CDRECORD;
      }
      else
	m_usedWritingApp = K3b::CDRECORD;
    }
    else
      m_usedWritingApp = writingApp();

    // on-the-fly writing with cdrecord >= 2.01a13
    if( m_usedWritingApp == K3b::CDRECORD &&
	m_doc->onTheFly() &&
	!cdrecordOnTheFly ) {
      emit infoMessage( i18n("On-the-fly writing with cdrecord < 2.01a13 not supported."), ERROR );
      m_doc->setOnTheFly(false);
    }

    if( m_usedWritingApp == K3b::CDRECORD &&
	m_doc->cdText() ) {
      if( !cdrecordCdText ) {
	emit infoMessage( i18n("Cdrecord %1 does not support CD-Text writing.").arg(k3bcore->externalBinManager()->binObject("cdrecord")->version), ERROR );
	m_doc->writeCdText(false);
      }
      else if( m_usedWritingMode == K3b::TAO ) {
	emit infoMessage( i18n("It is not possible to write CD-Text in TAO mode. Try DAO or RAW."), WARNING );
      }
    }
  }


  if( !m_doc->onlyCreateImages() && m_doc->onTheFly() ) {
    if( !prepareWriter() ) {
      cleanupAfterError();
      emit finished(false);
      return;
    }

    if( startWriting() ) {

      // now the writer is running and we can get it's stdin
      // we only use this method when writing on-the-fly since
      // we cannot easily change the audioDecode fd while it's working
      // which we would need to do since we write into several
      // image files.
      m_audioImager->writeToFd( m_writer->fd() );
    }
    else {
      // startWriting() already did the cleanup
      return;
    }
  }
  else {
    emit burning(false);
    emit infoMessage( i18n("Creating image files in %1").arg(m_doc->tempDir()), INFO );
    emit newTask( i18n("Creating image files") );
    m_tempData->prepareTempFileNames( doc()->tempDir() );
    QStringList filenames;
    for( int i = 1; i <= m_doc->numOfTracks(); ++i )
      filenames += m_tempData->bufferFileName( i );
    m_audioImager->setImageFilenames( filenames );
  }

  m_audioImager->start();
}


void K3bAudioJob::cancel()
{
  m_canceled = true;

  if( m_writer )
    m_writer->cancel();

  m_audioImager->cancel();
  emit infoMessage( i18n("Writing canceled."), K3bJob::ERROR );
  removeBufferFiles();
  emit canceled();
  emit finished(false);
}


void K3bAudioJob::slotWriterFinished( bool success )
{
  if( m_canceled || m_errorOccuredAndAlreadyReported )
    return;

  if( !success ) {
    cleanupAfterError();
    emit finished(false);
    return;
  }
  else {
    d->copiesDone++;

    if( d->copiesDone == d->copies ) {
      if( m_doc->onTheFly() || m_doc->removeImages() )
	removeBufferFiles();

      emit finished(true);
    }
    else {
      K3bDevice::eject( m_doc->burner() );

      if( startWriting() ) {
	if( m_doc->onTheFly() ) {
	  // now the writer is running and we can get it's stdin
	  // we only use this method when writing on-the-fly since
	  // we cannot easily change the audioDecode fd while it's working
	  // which we would need to do since we write into several
	  // image files.
	  m_audioImager->writeToFd( m_writer->fd() );
	  m_audioImager->start();
	}
      }
    }
  }
}


void K3bAudioJob::slotAudioDecoderFinished( bool success )
{
  if( m_canceled || m_errorOccuredAndAlreadyReported )
    return;

  if( !success ) {
    emit infoMessage( i18n("Error while decoding audio tracks."), ERROR );
    cleanupAfterError();
    emit finished(false);
    return;
  }

  if( m_doc->onlyCreateImages() || !m_doc->onTheFly() ) {

    emit infoMessage( i18n("Successfully decoded all tracks."), SUCCESS );

    if( m_doc->normalize() ) {
      normalizeFiles();
    }
    else if( !m_doc->onlyCreateImages() ) {
      if( !prepareWriter() ) {
	cleanupAfterError();
	emit finished(false);
      }
      else
	startWriting();
    }
    else {
      emit finished(true);
    }
  }
}


void K3bAudioJob::slotAudioDecoderNextTrack( int t, int tt )
{
  if( m_doc->onlyCreateImages() || !m_doc->onTheFly() ) {
    K3bAudioTrack* track = m_doc->getTrack(t-1);
    emit newSubTask( i18n("Decoding audio track %1 of %2%3")
		     .arg(t)
		     .arg(tt)
		     .arg( track->title().isEmpty() || track->artist().isEmpty() 
			   ? QString::null
			   : " (" + track->artist() + " - " + track->title() + ")" ) );
  }
}


bool K3bAudioJob::prepareWriter()
{
  delete m_writer;

  if( m_usedWritingApp == K3b::CDRECORD ) {

    if( !writeInfFiles() ) {
      kdDebug() << "(K3bAudioJob) could not write inf-files." << endl;
      emit infoMessage( i18n("IO Error"), ERROR );

      return false;
    }

    K3bCdrecordWriter* writer = new K3bCdrecordWriter( m_doc->burner(), this, this );

    writer->setWritingMode( m_usedWritingMode );
    writer->setSimulate( m_doc->dummy() );
    writer->setBurnSpeed( m_doc->speed() );

    writer->addArgument( "-useinfo" );

    if( m_doc->cdText() ) {
      writer->setRawCdText( m_doc->cdTextData().rawPackData() );
    }

    // we always pad because although K3b makes sure all tracks' lenght are multible of 2352
    // it seems that normalize sometimes corrupts these lengths
    writer->addArgument( "-pad" );

    // Allow tracks shorter than 4 seconds
    writer->addArgument( "-shorttrack" );

    // add all the audio tracks
    writer->addArgument( "-audio" );

    K3bAudioTrack* track = m_doc->firstTrack();
    while( track ) {
      if( m_doc->onTheFly() ) {
	// this is only supported by cdrecord versions >= 2.01a13
	writer->addArgument( QFile::encodeName( m_tempData->infFileName( track ) ) );
      }
      else {
	writer->addArgument( QFile::encodeName( m_tempData->bufferFileName( track ) ) );
      }
      track = track->next();
    }

    m_writer = writer;
  }
  else {
    if( !writeTocFile() ) {
      kdDebug() << "(K3bDataJob) could not write tocfile." << endl;
      emit infoMessage( i18n("IO Error"), ERROR );

      return false;
    }

    // create the writer
    // create cdrdao job
    K3bCdrdaoWriter* writer = new K3bCdrdaoWriter( m_doc->burner(), this, this );
    writer->setCommand( K3bCdrdaoWriter::WRITE );
    writer->setSimulate( m_doc->dummy() );
    writer->setBurnSpeed( m_doc->speed() );
    writer->setTocFile( m_tempData->tocFileName() );

    m_writer = writer;
  }

  connect( m_writer, SIGNAL(infoMessage(const QString&, int)), this, SIGNAL(infoMessage(const QString&, int)) );
  connect( m_writer, SIGNAL(percent(int)), this, SLOT(slotWriterJobPercent(int)) );
  connect( m_writer, SIGNAL(processedSize(int, int)), this, SIGNAL(processedSize(int, int)) );
  connect( m_writer, SIGNAL(subPercent(int)), this, SIGNAL(subPercent(int)) );
  connect( m_writer, SIGNAL(processedSubSize(int, int)), this, SIGNAL(processedSubSize(int, int)) );
  connect( m_writer, SIGNAL(nextTrack(int, int)), this, SLOT(slotWriterNextTrack(int, int)) );
  connect( m_writer, SIGNAL(buffer(int)), this, SIGNAL(bufferStatus(int)) );
  connect( m_writer, SIGNAL(deviceBuffer(int)), this, SIGNAL(deviceBuffer(int)) );
  connect( m_writer, SIGNAL(writeSpeed(int, int)), this, SIGNAL(writeSpeed(int, int)) );
  connect( m_writer, SIGNAL(finished(bool)), this, SLOT(slotWriterFinished(bool)) );
  //  connect( m_writer, SIGNAL(newTask(const QString&)), this, SIGNAL(newTask(const QString&)) );
  connect( m_writer, SIGNAL(newSubTask(const QString&)), this, SIGNAL(newSubTask(const QString&)) );
  connect( m_writer, SIGNAL(debuggingOutput(const QString&, const QString&)),
	   this, SIGNAL(debuggingOutput(const QString&, const QString&)) );

  return true;
}


void K3bAudioJob::slotWriterNextTrack( int t, int tt )
{
  K3bAudioTrack* track = m_doc->getTrack(t-1);
  // t is in range 1..tt
  emit newSubTask( i18n("Writing track %1 of %2%3")
		   .arg(t)
		   .arg(tt)
		   .arg( track->title().isEmpty() || track->artist().isEmpty() 
			 ? QString::null
			 : " (" + track->artist() + " - " + track->title() + ")" ) );
}


void K3bAudioJob::slotWriterJobPercent( int p )
{
  double totalTasks = d->copies;
  double tasksDone = d->copiesDone;
  if( m_doc->normalize() ) {
    totalTasks+=1.0;
    tasksDone+=1.0;
  }
  if( !m_doc->onTheFly() ) {
    totalTasks+=1.0;
    tasksDone+=1.0;
  }

  emit percent( (int)((100.0*tasksDone + (double)p) / totalTasks) );
}


void K3bAudioJob::slotAudioDecoderPercent( int p )
{
  if( m_doc->onlyCreateImages() ) {
    if( m_doc->normalize() )
      emit percent( p/2 );
    else
      emit percent( p );
  }
  else if( !m_doc->onTheFly() ) {
    double totalTasks = d->copies;
    double tasksDone = d->copiesDone; // =0 when creating an image
    if( m_doc->normalize() ) {
      totalTasks+=1.0;
    }
    if( !m_doc->onTheFly() ) {
      totalTasks+=1.0;
    }

    emit percent( (int)((100.0*tasksDone + (double)p) / totalTasks) );
  }
}


void K3bAudioJob::slotAudioDecoderSubPercent( int p )
{
  // when writing on the fly the writer produces the subPercent
  if( m_doc->onlyCreateImages() || !m_doc->onTheFly() ) {
    emit subPercent( p );
  }
}


bool K3bAudioJob::startWriting()
{
  if( m_doc->dummy() )
    emit newTask( i18n("Simulating") );
  else if( d->copies > 1 )
    emit newTask( i18n("Writing Copy %1").arg(d->copiesDone+1) );
  else
    emit newTask( i18n("Writing") );


  emit newSubTask( i18n("Waiting for media") );
  if( waitForMedia( m_doc->burner() ) < 0 ) {
    cancel();
    return false;
  }

  // just to be sure we did not get canceled during the async discWaiting
  if( m_canceled )
    return false;

  emit burning(true);
  m_writer->start();
  return true;
}


void K3bAudioJob::cleanupAfterError()
{
  m_errorOccuredAndAlreadyReported = true;
  m_audioImager->cancel();

  if( m_writer )
    m_writer->cancel();

  // remove the temp files
  removeBufferFiles();
}


void K3bAudioJob::removeBufferFiles()
{
  emit infoMessage( i18n("Removing temporary files."), INFO );

  m_tempData->cleanup();
}


void K3bAudioJob::normalizeFiles()
{
  if( !m_normalizeJob ) {
    m_normalizeJob = new K3bAudioNormalizeJob( this, this );

    connect( m_normalizeJob, SIGNAL(infoMessage(const QString&, int)),
	     this, SIGNAL(infoMessage(const QString&, int)) );
    connect( m_normalizeJob, SIGNAL(percent(int)), this, SLOT(slotNormalizeProgress(int)) );
    connect( m_normalizeJob, SIGNAL(subPercent(int)), this, SLOT(slotNormalizeSubProgress(int)) );
    connect( m_normalizeJob, SIGNAL(finished(bool)), this, SLOT(slotNormalizeJobFinished(bool)) );
    connect( m_normalizeJob, SIGNAL(newTask(const QString&)), this, SIGNAL(newSubTask(const QString&)) );
    connect( m_normalizeJob, SIGNAL(debuggingOutput(const QString&, const QString&)),
	     this, SIGNAL(debuggingOutput(const QString&, const QString&)) );
  }

  // add all the files
  // TODO: we may need to split the wave files and put them back together!
  QValueVector<QString> files;
  K3bAudioTrack* track = m_doc->firstTrack();
  while( track ) {
    files.append( m_tempData->bufferFileName(track) );
    track = track->next();
  }

  m_normalizeJob->setFilesToNormalize( files );

  emit newTask( i18n("Normalizing volume levels") );
  m_normalizeJob->start();
}

void K3bAudioJob::slotNormalizeJobFinished( bool success )
{
  if( m_canceled || m_errorOccuredAndAlreadyReported )
    return;

  if( success ) {
    if( m_doc->onlyCreateImages() ) {
      emit finished(true);
    }
    else {
      // start the writing
      if( !prepareWriter() ) {
	cleanupAfterError();
	emit finished(false);
      }
      else
	startWriting();
    }
  }
  else {
    cleanupAfterError();
    emit finished(false);
  }
}

void K3bAudioJob::slotNormalizeProgress( int p )
{
  double totalTasks = d->copies+2.0;
  double tasksDone = 1; // the decoding has been finished
  
  emit percent( (int)((100.0*tasksDone + (double)p) / totalTasks) );
}


void K3bAudioJob::slotNormalizeSubProgress( int p )
{
  emit subPercent( p );
}


bool K3bAudioJob::writeTocFile()
{
  K3bTocFileWriter tocWriter;
  tocWriter.setData( m_doc->toToc() );
  tocWriter.setHideFirstTrack( m_doc->hideFirstTrack() );
  if( m_doc->cdText() )
    tocWriter.setCdText( m_doc->cdTextData() );
  if( !m_doc->onTheFly() ) {
    QStringList filenames;
    for( int i = 1; i <= m_doc->numOfTracks(); ++i )
      filenames += m_tempData->bufferFileName( i );
    tocWriter.setFilenames( filenames );
  }
  return tocWriter.save( m_tempData->tocFileName() );
}


bool K3bAudioJob::writeInfFiles()
{
  K3bInfFileWriter infFileWriter;
  K3bAudioTrack* track = m_doc->firstTrack();
  while( track ) {

    infFileWriter.setTrack( track->toCdTrack() );
    infFileWriter.setTrackNumber( track->index()+1 );
    if( !m_doc->onTheFly() )
      infFileWriter.setBigEndian( false );

    if( !infFileWriter.save( m_tempData->infFileName(track) ) )
      return false;

    track = track->next();
  }
  return true;
}


QString K3bAudioJob::jobDescription() const
{
  return i18n("Writing Audio CD")
    + ( m_doc->title().isEmpty() 
	? QString::null
	: QString( " (%1)" ).arg(m_doc->title()) );
}


QString K3bAudioJob::jobDetails() const
{
  return ( i18n( "1 track (%1 minutes)", 
		 "%n tracks (%1 minutes)", 
		 m_doc->numOfTracks() ).arg(m_doc->length().toString())
	   + ( m_doc->copies() > 1 && !m_doc->dummy()
	       ? i18n(" - %n copy", " - %n copies", m_doc->copies()) 
	       : QString::null ) );
}

#include "k3baudiojob.moc"
