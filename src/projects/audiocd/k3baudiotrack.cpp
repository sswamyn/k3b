/* 
 *
 * $Id$
 * Copyright (C) 2003 Sebastian Trueg <trueg@k3b.org>
 *
 * This file is part of the K3b project.
 * Copyright (C) 1998-2003 Sebastian Trueg <trueg@k3b.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * See the file "COPYING" for the exact licensing terms.
 */


#include "k3baudiotrack.h"

#include <k3baudiodecoder.h>
#include <k3bcore.h>

#include <kconfig.h>

#include <qstring.h>
#include <qfileinfo.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <kdebug.h>



K3bAudioTrack::K3bAudioTrack( QPtrList<K3bAudioTrack>* parent, const QString& filename )
  : m_file(filename),
    m_length(0),
    m_status(0)
{
  m_parent = parent;
  m_copy = false;
  m_preEmp = false;
  
  k3bcore->config()->setGroup( "Audio project settings" );
  setPregap( k3bcore->config()->readNumEntry( "default pregap", 150 ) );
  

  m_module = 0;
}


K3bAudioTrack::~K3bAudioTrack()
{
  delete m_module;
}


K3b::Msf K3bAudioTrack::length() const
{
  return( m_module ? m_module->length() : 0 );
}


KIO::filesize_t K3bAudioTrack::size() const
{
  return length().audioBytes();
}


int K3bAudioTrack::index() const
{
  int i = m_parent->find( this );
  if( i < 0 )
    kdDebug() << "(K3bAudioTrack) I'm not part of my parent!" << endl;
  return i;
}

// void K3bAudioTrack::setBufferFile( const QString& path )
// {
//   m_bufferFile = path;
// }


void K3bAudioTrack::setPregap( const K3b::Msf& p )
{
  m_pregap = p; 
}


void K3bAudioTrack::setModule( K3bAudioDecoder* module )
{
  m_module = module;
  m_module->setFilename( absPath() );
}
