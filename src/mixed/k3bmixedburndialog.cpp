#include "k3bmixedburndialog.h"
#include "k3bmixeddoc.h"

#include <data/k3bdataimagesettingswidget.h>
#include <data/k3bdataadvancedimagesettingswidget.h>
#include <data/k3bdatavolumedescwidget.h>
#include <data/k3bdatadoc.h>
#include <device/k3bdevice.h>
#include <k3bwriterselectionwidget.h>
#include <k3btempdirselectionwidget.h>
#include <k3bisooptions.h>
#include <tools/k3bglobals.h>
#include <audio/k3baudiocdtextwidget.h>


#include <qtabwidget.h>
#include <qcheckbox.h>
#include <qframe.h>
#include <qgroupbox.h>
#include <qlabel.h>
#include <qlineedit.h>
#include <qpushbutton.h>
#include <qtoolbutton.h>
#include <qlayout.h>
#include <qvariant.h>
#include <qtooltip.h>
#include <qwhatsthis.h>
#include <qvbox.h>
#include <qbuttongroup.h>
#include <qradiobutton.h>

#include <klocale.h>
#include <kconfig.h>
#include <kapplication.h>
#include <kdebug.h>


K3bMixedBurnDialog::K3bMixedBurnDialog( K3bMixedDoc* doc, QWidget *parent, const char *name, bool modal )
  : K3bProjectBurnDialog( doc, parent, name, modal ), m_doc(doc)
{
  prepareGui();

  setupSettingsPage();

  // create cd-text page
  m_cdtextWidget = new K3bAudioCdTextWidget( this );
  addPage( m_cdtextWidget, i18n("CD-Text") );

  // create volume descriptor tab
  m_volumeDescWidget = new K3bDataVolumeDescWidget( this );
  m_volumeDescWidget->layout()->setMargin( marginHint() );
  addPage( m_volumeDescWidget, i18n("Volume Desc") );

  // create image settings tab
  m_imageSettingsWidget = new K3bDataImageSettingsWidget( this );
  m_imageSettingsWidget->layout()->setMargin( marginHint() );
  addPage( m_imageSettingsWidget, i18n("Data Settings") );

  // create advanced image settings tab
  m_advancedImageSettingsWidget = new K3bDataAdvancedImageSettingsWidget( this );
  m_advancedImageSettingsWidget->layout()->setMargin( marginHint() );
  addPage( m_advancedImageSettingsWidget, i18n("Advanced") );

  createContextHelp();

  connect( m_checkDao, SIGNAL(toggled(bool)), this, SLOT(slotToggleEverything()) );
  connect( m_writerSelectionWidget, SIGNAL(writingAppChanged(int)), this, SLOT(slotToggleEverything()) );

  slotWriterChanged();
}


void K3bMixedBurnDialog::setupSettingsPage()
{
  QWidget* w = new QWidget( k3bMainWidget() );
  m_groupMixedType = new QButtonGroup( 4, Qt::Vertical, i18n("Mixed mode type"), w );
  // standard mixed mode
  m_radioMixedTypeFirstTrack = new QRadioButton( i18n("Data in first track"), m_groupMixedType );
  // is this a standard?
  m_radioMixedTypeLastTrack = new QRadioButton( i18n("Data in last track"), m_groupMixedType );

  // Enhanced music CD/CD Extra/CD Plus format (Blue Book) 
  // to fulfill the standard we also need the special file structure
  // but in the case of our simple mixed mode cd we allow to create blue book cds without
  // these special files and directories
  m_radioMixedTypeSessions = new QRadioButton( i18n("Data in second session (CD-Extra)"), m_groupMixedType );
  m_groupMixedType->setExclusive(true);

  QGridLayout* grid = new QGridLayout( w );
  grid->setMargin( marginHint() );
  grid->setSpacing( spacingHint() );
  grid->addWidget( m_groupMixedType, 0, 0 );

  addPage( w, i18n("Settings") );
}


void K3bMixedBurnDialog::createContextHelp()
{
  QToolTip::add( m_radioMixedTypeFirstTrack, i18n("First track will contain the data") );
  QWhatsThis::add( m_radioMixedTypeFirstTrack, i18n("<p><b>Standard mixed mode cd 1</b>"
						    "<p>K3b will write the data track before all "
						    "audio tracks."
						    "<p>This mode should only be used for cds that are unlikely to "
						    "be played on a hifi audio cd player."
						    "<p><b>Caution:</b> It could lead to problems with some older "
						    "hifi audio cd player that try to play the data track.") );

  QToolTip::add( m_radioMixedTypeLastTrack, i18n("Last track will contain the data") );
  QWhatsThis::add( m_radioMixedTypeLastTrack, i18n("<p><b>Standard mixed mode cd 2</b>"
						   "<p>K3b will write the data track after all "
						   "audio tracks."
						    "<p>This mode should only be used for cds that are unlikely to "
						    "be played on a hifi audio cd player."
						   "<p><b>Caution:</b> It could lead to problems with some older "
						   "hifi audio cd player that try to play the data track.") );

  QToolTip::add( m_radioMixedTypeSessions, i18n("The data will be written in a second session") );
  QWhatsThis::add( m_radioMixedTypeSessions, i18n("<p><b>Blue book cd</b>"
						  "<p>K3b will create a multisession cd with "
						  "2 sessions. The first session will contain all "
						  "audio tracks and the second session will contain "
						  "a mode 2 form 1 data track."
						  "<p>This mode is based on the <em>Blue book</em> "
						  "standard (also known as <em>Extended Audio CD</em>, "
						  "<em>CD-Extra</em>, or <em>CD Plus</em>) "
						  "and has the advantage that a hifi audio "
						  "cd player will just recognize the first session "
						  "and ignore the second session with the data track."
						  "<p>If the cd is intended to be used in a hifi audio cd player "
						  "this is the recommended mode."
						  "<p>Some older CD-ROMs may have problems reading "
						  "a blue book cd since it's a multisession cd.") );
}


void K3bMixedBurnDialog::saveSettings()
{
  m_doc->setDao( m_checkDao->isChecked() );
  m_doc->setDummy( m_checkSimulate->isChecked() );
  m_doc->setOnTheFly( m_checkOnTheFly->isChecked() );
  m_doc->setBurnproof( m_checkBurnproof->isChecked() );
  m_doc->setRemoveBufferFiles( m_checkRemoveBufferFiles->isChecked() );
			
  // -- saving current speed --------------------------------------
  m_doc->setSpeed( m_writerSelectionWidget->writerSpeed() );
	
  // -- saving current device --------------------------------------
  m_doc->setBurner( m_writerSelectionWidget->writerDevice() );

  if( m_groupMixedType->selected() == m_radioMixedTypeLastTrack )
    m_doc->setMixedType( K3bMixedDoc::DATA_LAST_TRACK );
  else if( m_groupMixedType->selected() == m_radioMixedTypeSessions )
    m_doc->setMixedType( K3bMixedDoc::DATA_SECOND_SESSION );
  else
    m_doc->setMixedType( K3bMixedDoc::DATA_FIRST_TRACK );

  m_cdtextWidget->save( m_doc->audioDoc() );

  // save iso image settings
  m_imageSettingsWidget->save( m_doc->dataDoc()->isoOptions() );
  m_advancedImageSettingsWidget->save( m_doc->dataDoc()->isoOptions() );
  m_volumeDescWidget->save( m_doc->dataDoc()->isoOptions() );
	

  // save image file path
  m_doc->setImagePath( m_tempDirSelectionWidget->tempPath() );  
}


void K3bMixedBurnDialog::readSettings()
{
  m_checkDao->setChecked( doc()->dao() );
  m_checkSimulate->setChecked( doc()->dummy() );
  m_checkOnTheFly->setChecked( doc()->onTheFly() );
  m_checkBurnproof->setChecked( doc()->burnproof() );
  m_checkRemoveBufferFiles->setChecked( m_doc->removeBufferFiles() );
	

  if( !m_doc->imagePath().isEmpty() )
    m_tempDirSelectionWidget->setTempPath( m_doc->imagePath() );

  switch( m_doc->mixedType() ) {
  case K3bMixedDoc::DATA_FIRST_TRACK:
    m_radioMixedTypeFirstTrack->setChecked(true);
    break;
  case K3bMixedDoc::DATA_LAST_TRACK:
    m_radioMixedTypeLastTrack->setChecked(true);
    break;
  case K3bMixedDoc::DATA_SECOND_SESSION:
    m_radioMixedTypeSessions->setChecked(true);
    break;
  }

  m_cdtextWidget->load( m_doc->audioDoc() );

  m_imageSettingsWidget->load( m_doc->dataDoc()->isoOptions() );
  m_advancedImageSettingsWidget->load( m_doc->dataDoc()->isoOptions() );
  m_volumeDescWidget->load( m_doc->dataDoc()->isoOptions() );

  K3bProjectBurnDialog::readSettings();
}


void K3bMixedBurnDialog::loadDefaults()
{
   m_checkSimulate->setChecked( false );
   m_checkDao->setChecked( true );
   m_checkOnTheFly->setChecked( true );
   m_checkBurnproof->setChecked( true );
   m_checkRemoveBufferFiles->setChecked( true );
   m_cdtextWidget->setChecked( false );

   m_radioMixedTypeFirstTrack->setChecked(true);

   m_imageSettingsWidget->load( K3bIsoOptions::defaults() );
   m_advancedImageSettingsWidget->load( K3bIsoOptions::defaults() );
   m_volumeDescWidget->load( K3bIsoOptions::defaults() );
}


void K3bMixedBurnDialog::loadUserDefaults()
{
  KConfig* c = kapp->config();
  c->setGroup( "default mixed settings" );

  m_checkSimulate->setChecked( c->readBoolEntry( "dummy_mode", false ) );
  m_checkDao->setChecked( c->readBoolEntry( "dao", true ) );
  m_checkOnTheFly->setChecked( c->readBoolEntry( "on_the_fly", true ) );
  m_checkBurnproof->setChecked( c->readBoolEntry( "burnproof", true ) );
  m_checkRemoveBufferFiles->setChecked( c->readBoolEntry( "remove_buffer_files", true ) );

  m_cdtextWidget->setChecked( c->readBoolEntry( "cd_text", false ) );

  // load mixed type
  if( c->readEntry( "mixed_type" ) == "last_track" )
    m_radioMixedTypeLastTrack->setChecked(true);
  else if( c->readEntry( "mixed_type" ) == "second_session" )
    m_radioMixedTypeSessions->setChecked(true);
  else
    m_radioMixedTypeFirstTrack->setChecked(true);

  K3bIsoOptions o = K3bIsoOptions::load( c );
  m_imageSettingsWidget->load( o );
  m_advancedImageSettingsWidget->load( o );
  m_volumeDescWidget->load( o );
}


void K3bMixedBurnDialog::saveUserDefaults()
{
  KConfig* c = kapp->config();

  c->setGroup( "default mixed settings" );
  
  c->writeEntry( "dummy_mode", m_checkSimulate->isChecked() );
  c->writeEntry( "dao", m_checkDao->isChecked() );
  c->writeEntry( "on_the_fly", m_checkOnTheFly->isChecked() );
  c->writeEntry( "burnproof", m_checkBurnproof->isChecked() );
  c->writeEntry( "remove_buffer_files", m_checkRemoveBufferFiles->isChecked() );

  c->writeEntry( "cd_text", m_cdtextWidget->isChecked() );

  // save mixed type
  if( m_groupMixedType->selected() == m_radioMixedTypeLastTrack )
   c->writeEntry( "mixed_type", "last_track" );
  else if( m_groupMixedType->selected() == m_radioMixedTypeSessions )
   c->writeEntry( "mixed_type", "second_session" );
  else
    c->writeEntry( "mixed_type", "first_track" );

  K3bIsoOptions o;
  m_imageSettingsWidget->save( o );
  m_advancedImageSettingsWidget->save( o );
  m_volumeDescWidget->save( o );
  o.save( c );


//   if( m_tempDirSelectionWidget->isEnabled() ) {
//     kapp->config()->setGroup( "General Options" );
//     QFileInfo fi( m_tempDirSelectionWidget->tempPath() );
//     QString path;
//     if( fi.isFile() )
//       path = fi.dirPath();
//     else
//       path = fi.filePath();

//     kapp->config()->writeEntry( "Temp Dir", path );
//   }
}


void K3bMixedBurnDialog::slotOk()
{
  K3bProjectBurnDialog::slotOk();
}


void K3bMixedBurnDialog::slotToggleEverything()
{
  // currently we do not support writing on the fly with cdrecord
  if( !m_checkDao->isChecked() || m_writerSelectionWidget->writingApp() == K3b::CDRECORD ) {
    m_checkOnTheFly->setEnabled( false );
    m_checkOnTheFly->setChecked( false );
  }
  else {
    m_checkOnTheFly->setEnabled( true );
  }
}


#include "k3bmixedburndialog.moc"

