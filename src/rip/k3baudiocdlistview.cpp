/* 
 *
 * $Id$
 * Copyright (C) 2004 Sebastian Trueg <trueg@k3b.org>
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

#include "k3baudiocdlistview.h"
#include "k3baudiocdview.h"

#include <klocale.h>

#include <qheader.h>


K3bAudioCdListView::K3bAudioCdListView( K3bAudioCdView* view, QWidget* parent, const char* name )
  : K3bListView( parent, name ),
    m_view(view)
{
  setFullWidth(true);
  setSorting(-1);
  setAllColumnsShowFocus( true );
  setSelectionMode( QListView::Single );
  setDragEnabled( true );
  addColumn( "" );
  addColumn( "" );
  addColumn( i18n("Artist") );
  addColumn( i18n("Title") );
  addColumn( i18n("Length") );
  addColumn( i18n("Size") );

  header()->setClickEnabled(false);
  setColumnWidthMode( 0, QListView::Manual );
  setColumnWidth( 0, 20 );
  header()->setResizeEnabled( false,0 );

  setItemsRenameable(true);
  setRenameable(0, false);
  setRenameable(1, false);
  setRenameable(2, true);
  setRenameable(3, true);

  setColumnAlignment( 4, Qt::AlignHCenter );
}


K3bAudioCdListView::~K3bAudioCdListView()
{
}


QDragObject* K3bAudioCdListView::dragObject()
{
  return m_view->dragObject();
}


#include "k3baudiocdlistview.moc"

