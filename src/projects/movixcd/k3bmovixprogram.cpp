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


#include "k3bmovixprogram.h"

#include <k3bprocess.h>

#include <kdebug.h>
#include <klocale.h>

#include <qdir.h>
#include <qfile.h>
#include <qtextstream.h>


K3bMovixProgram::K3bMovixProgram()
  : K3bExternalProgram( "eMovix" )
{
}

bool K3bMovixProgram::scan( const QString& p )
{
  if( p.isEmpty() )
    return false;

  QString path = p;
  if( path[path.length()-1] != '/' )
    path.append("/");

  // first test if we have a version info (eMovix >= 0.8.0pre3)
  if( !QFile::exists( path + "movix-version" ) )
    return false;

  K3bMovixBin* bin = 0;

  // probe version
  KProcess vp;
  vp << path + "movix-version";
  K3bProcess::OutputCollector out( &vp );
  if( vp.start( KProcess::Block, KProcess::AllOutput ) ) {
    // movix-version just gives us the version number on stdout
    if( !out.output().isEmpty() ) {
      bin = new K3bMovixBin( this );
      bin->version = out.output().stripWhiteSpace();
    }
  }
  else {
    kdDebug() << "(K3bMovixProgram) could not start " << path << "movix-version" << endl;
    return false;
  }


  //
  // we have a valid version
  // now search for the config (the movix files base dir)
  //
  KProcess cp;
  cp << path + "movix-conf";
  out.setProcess( &cp );
  if( cp.start( KProcess::Block, KProcess::AllOutput ) ) {
    // now search the needed files in the given dir
    if( out.output().isEmpty() ) {
      kdDebug() << "(K3bMovixProgram) no eMovix config info" << endl;
      delete bin;
      return false;
    }

    // save the path to all the movix scripts.
    bin->path = out.output().stripWhiteSpace();

    //
    // first check if all necessary directories are present
    //
    QDir dir(bin->path);
    QStringList subdirs = dir.entryList( QDir::Dirs );
    if( !subdirs.contains( "boot-messages" ) ) {
      kdDebug() << "(K3bMovixProgram) could not find subdir 'boot-messages'" << endl;
      delete bin;
      return false;
    }
    if( !subdirs.contains( "isolinux" ) ) {
      kdDebug() << "(K3bMovixProgram) could not find subdir 'isolinux'" << endl;
      delete bin;
      return false;
    }
    if( !subdirs.contains( "movix" ) ) {
      kdDebug() << "(K3bMovixProgram) could not find subdir 'movix'" << endl;
      delete bin;
      return false;
    }
    if( !subdirs.contains( "mplayer-fonts" ) ) {
      kdDebug() << "(K3bMovixProgram) could not find subdir 'mplayer-fonts'" << endl;
      delete bin;
      return false;
    }


    //
    // check if we have a version of eMovix which contains the movix-files script
    //
    if( QFile::exists( path + "movix-files" ) ) {
      bin->addFeature( "files" );

      KProcess p;
      K3bProcess::OutputCollector out( &p );
      p << bin->path + "movix-files";
      if( p.start( KProcess::Block, KProcess::AllOutput ) ) {
	bin->m_movixFiles = QStringList::split( "\n", out.output() );
      }
    }

    //
    // fallback: to be compatible with 0.8.0rc2 we just add all files in the movix directory
    //
    if( bin->m_movixFiles.isEmpty() ) {
      QDir dir( bin->path + "/movix" );
      bin->m_movixFiles = dir.entryList(QDir::Files);
    }

    //
    // these files are fixed. That should not be a problem
    // since Isolinux is quite stable as far as I know.
    //
    bin->m_isolinuxFiles.append( "initrd.gz" );
    bin->m_isolinuxFiles.append( "isolinux.bin" );
    bin->m_isolinuxFiles.append( "isolinux.cfg" );
    bin->m_isolinuxFiles.append( "kernel/vmlinuz" );
    bin->m_isolinuxFiles.append( "movix.lss" );
    bin->m_isolinuxFiles.append( "movix.msg" );


    //
    // check every single necessary file :(
    //
    for( QStringList::const_iterator it = bin->m_isolinuxFiles.begin();
	 it != bin->m_isolinuxFiles.end(); ++it ) {
      if( !QFile::exists( bin->path + "/isolinux/" + *it ) ) {
	kdDebug() << "(K3bMovixProgram) Could not find file " << *it << endl;
	delete bin;
	return false;
      }
    }

    //
    // now check the boot-messages languages
    //
    dir.cd( "boot-messages" );
    bin->m_supportedLanguages = dir.entryList(QDir::Dirs);
    bin->m_supportedLanguages.remove(".");
    bin->m_supportedLanguages.remove("..");
    bin->m_supportedLanguages.remove("CVS");  // the eMovix makefile stuff seems not perfect ;)
    bin->m_supportedLanguages.prepend( i18n("default") );
    dir.cdUp();

    //
    // now check the supported mplayer-fontsets
    // FIXME: every font dir needs to contain the "font.desc" file!
    //
    dir.cd( "mplayer-fonts" );
    bin->m_supportedSubtitleFonts = dir.entryList( QDir::Dirs );
    bin->m_supportedSubtitleFonts.remove(".");
    bin->m_supportedSubtitleFonts.remove("..");
    bin->m_supportedSubtitleFonts.remove("CVS");  // the eMovix makefile stuff seems not perfect ;)
    // new ttf fonts in 0.8.0rc2
    bin->m_supportedSubtitleFonts += dir.entryList( "*.ttf", QDir::Files );
    bin->m_supportedSubtitleFonts.prepend( i18n("none") );
    dir.cdUp();
  
    //
    // now check the supported boot labels
    //
    dir.cd( "isolinux" );
    QFile f( dir.filePath("isolinux.cfg") );
    if( !f.open( IO_ReadOnly ) ) {
      kdDebug() << "(K3bMovixProgram) could not open file '" << f.name() << "'" << endl;
      delete bin;
      return false;
    }
    QTextStream fs( &f );
    QString line = fs.readLine();
    while( !line.isNull() ) {
      if( line.startsWith( "label" ) ) {
	bin->m_supportedBootLabels.append( line.mid( 5 ).stripWhiteSpace() );
      }
      line = fs.readLine();
    }
    f.close();
    bin->m_supportedBootLabels.prepend( i18n("default") );


    //
    // This seems to be a valid eMovix installation. :)
    //

    addBin(bin);
    return true;
  }
  else {
    kdDebug() << "(K3bMovixProgram) could not start " << path << endl;
    delete bin;
    return false;
  }
}



QString K3bMovixBin::subtitleFontDir( const QString& font ) const
{
  if( font == i18n("none" ) )
    return "";
  else if( m_supportedSubtitleFonts.contains( font ) )
    return path + "/mplayer-fonts/" + font;
  else
    return "";
}


QString K3bMovixBin::languageDir( const QString& lang ) const
{
  if( lang == i18n("default") )
    return languageDir( "en" );
  else if( m_supportedLanguages.contains( lang ) )
    return path + "/boot-messages/" + lang;
  else
    return "";
}
