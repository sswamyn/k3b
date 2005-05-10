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


#include "k3bfileview.h"
#include "k3b.h"
#include "k3bdiroperator.h"
#include "k3btoolbox.h"
#include "k3bapplication.h"

#include <qwidget.h>
#include <qdragobject.h>
#include <qlayout.h>
#include <qdir.h>
#include <qvbox.h>
#include <qlabel.h>
#include <qtoolbutton.h>

#include <kfiledetailview.h>
#include <klistview.h>
#include <kaction.h>
#include <ktoolbar.h>
#include <ktoolbarbutton.h>
#include <kurl.h>
#include <kurldrag.h>
#include <kfilefiltercombo.h>
#include <klocale.h>
#include <kfileitem.h>
#include <kmessagebox.h>
#include <kdirlister.h>


K3bFileView::K3bFileView(QWidget *parent, const char *name ) 
  : K3bCdContentsView(false, parent,name) 
{
  setupGUI();
}


K3bFileView::~K3bFileView()
{
}


KActionCollection* K3bFileView::actionCollection() const
{
  return m_dirOp->actionCollection();
}


void K3bFileView::setupGUI()
{
  QVBoxLayout* layout = new QVBoxLayout( this );
  //  layout->setAutoAdd( true );

  m_dirOp = new K3bDirOperator( KURL::fromPathOrURL(QDir::home().absPath()), this );

  m_toolBox = new K3bToolBox( this );

  layout->addWidget( m_toolBox );
  layout->addWidget( m_dirOp );
  layout->setStretchFactor( m_dirOp, 1 );


  // setup actions
  KAction* actionAddFilesToProject = new KAction( i18n("&Add to Project"), SHIFT+Key_Return, 
						  this, SLOT(slotAddFilesToProject()), 
						  m_dirOp->actionCollection(), "add_file_to_project");

  KAction* actionHome = m_dirOp->actionCollection()->action("home");
  KAction* actionBack = m_dirOp->actionCollection()->action("back");
  KAction* actionUp = m_dirOp->actionCollection()->action("up");
  KAction* actionReload = m_dirOp->actionCollection()->action("reload");


  m_toolBox->addButton( actionUp );
  m_toolBox->addButton( actionBack );
  m_toolBox->addButton( actionHome );
  m_toolBox->addButton( actionReload );
  m_toolBox->addSpacing();
  m_toolBox->addButton( m_dirOp->bookmarkMenu() );
  m_toolBox->addSpacing();

  // insert actions into diroperator menu
  // FIXME: this does not work anymore since KDirOperator always clears the menu
  KActionMenu* dirOpMenu = (KActionMenu*)m_dirOp->actionCollection()->action("popupMenu");
  dirOpMenu->insert( actionAddFilesToProject, 0 );
  dirOpMenu->insert( new KActionSeparator( m_dirOp->actionCollection() ), 1 );

  // check if some actions should be enabled
  connect( dirOpMenu, SIGNAL(activated()), this, SLOT(slotCheckActions()) );

  // create filter selection combobox
  m_toolBox->addSpacing();
  m_toolBox->addLabel( i18n("Filter:") );
  m_filterWidget = new KFileFilterCombo( m_toolBox, "filterwidget" );
  m_toolBox->addWidget( m_filterWidget );
  m_toolBox->addStretch();

  m_filterWidget->setEditable( true );
  QString filter = i18n("*|All Files");
  filter += "\n" + i18n("audio/x-mp3 audio/x-wav application/x-ogg |Sound Files");
  filter += "\n" + i18n("audio/x-wav |Wave Sound Files");
  filter += "\n" + i18n("audio/x-mp3 |MP3 Sound Files");
  filter += "\n" + i18n("application/x-ogg |Ogg Vorbis Sound Files");
  filter += "\n" + i18n("video/mpeg |MPEG Video Files");
  m_filterWidget->setFilter(filter);

  connect( m_filterWidget, SIGNAL(filterChanged()), SLOT(slotFilterChanged()) );

  connect( m_dirOp, SIGNAL(fileHighlighted(const KFileItem*)), this, SLOT(slotFileHighlighted(const KFileItem*)) );
  connect( m_dirOp, SIGNAL(urlEntered(const KURL&)), this, SIGNAL(urlEntered(const KURL&)) );
  connect( m_dirOp, SIGNAL(doubleClicked(KFileItem*)), this, SLOT(slotAddFilesToProject()) );

  slotFileHighlighted(0);
}

void K3bFileView::setDir( const QString& dir )
{
  KURL url;
  url.setPath(dir);
  setUrl( url );
}


void K3bFileView::setUrl(const KURL& url, bool forward)
{
  m_dirOp->setURL( url, forward );
}

KURL K3bFileView::url()
{
  return m_dirOp->url();
}

void K3bFileView::setAutoUpdate( bool b )
{
  m_dirOp->dirLister()->setAutoUpdate( b );
}

void K3bFileView::slotFileHighlighted( const KFileItem* )
{
}


void K3bFileView::slotAddFilesToProject()
{
  KURL::List files;
  for( QPtrListIterator<KFileItem> it( *(m_dirOp->selectedItems()) ); it.current(); ++it ) {
    files.append( it.current()->url() );
  }    
  if( !files.isEmpty() )
    k3bappcore->k3bMainWindow()->addUrls( files );
}


void K3bFileView::slotFilterChanged()
{
  QString filter = m_filterWidget->currentFilter();
  m_dirOp->clearFilter();

  if( filter.find( '/' ) > -1 ) {
    QStringList types = QStringList::split( " ", filter );
    types.prepend( "inode/directory" );
    m_dirOp->setMimeFilter( types );
  }
  else
    m_dirOp->setNameFilter( filter );
  
  m_dirOp->rereadDir();
  //  emit filterChanged( filter );
}


void K3bFileView::slotCheckActions()
{
  m_dirOp->actionCollection()->action("add_file_to_project")->setEnabled( k3bappcore->k3bMainWindow()->activeView() != 0 );
}


void K3bFileView::reload()
{
  m_dirOp->actionCollection()->action("reload")->activate();
}


void K3bFileView::saveConfig( KConfig* c )
{
  m_dirOp->writeConfig( c, "file view" );
}


#include "k3bfileview.moc"
