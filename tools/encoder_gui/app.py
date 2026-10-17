"""A native, responsive front end to the nMedia encoder."""
from pathlib import Path
import os,sys,time
from PySide6.QtCore import Qt,QTimer,QSettings,QUrl
from PySide6.QtGui import QPixmap,QDesktopServices,QKeySequence,QShortcut,QPalette,QColor,QFontDatabase,QIcon
from PySide6.QtWidgets import (QApplication,QMainWindow,QWidget,QLabel,QPushButton,QToolButton,QLineEdit,
    QComboBox,QSpinBox,QDoubleSpinBox,QCheckBox,QRadioButton,QButtonGroup,QFrame,QVBoxLayout,QHBoxLayout,
    QGridLayout,QFormLayout,QTabWidget,QScrollArea,QTreeWidget,QTreeWidgetItem,QHeaderView,
    QProgressBar,QPlainTextEdit,QFileDialog,QMessageBox)
from . import VERSION
from .model import EncodeOptions,AudioOptions,TrackEditOptions,VIDEO_CODECS,AUDIO_CODECS,LANGUAGES,track_label,duration_text,size_text,output_path,cli_command
from convert_audio import output_path as audio_path,EXTENSIONS
from .widgets import SourceCard,Preview,STYLES,icon
from .worker import Task,encode_task

def muted(text):
    label=QLabel(text);label.setObjectName('muted');label.setWordWrap(True);label.setTextFormat(Qt.TextFormat.PlainText)
    return label

def combo(entries):
    control=QComboBox()
    for text,data in entries:control.addItem(text,data)
    return control

def spin(low,high,value,decimals=None):
    widget=QSpinBox() if decimals is None else QDoubleSpinBox()
    if decimals is not None:widget.setDecimals(decimals)
    widget.setRange(low,high);widget.setValue(value);widget.setMinimumWidth(96)
    return widget

def horizontal(*widgets):
    box=QWidget();row=QHBoxLayout(box);row.setContentsMargins(0,0,0,0);row.setSpacing(10)
    for widget in widgets:row.addWidget(widget)
    return box

class SettingsForm(QFormLayout):
    def addRow(self,label,field):
        if isinstance(label,str):label=QLabel(label)
        label.setMinimumWidth(112)
        if isinstance(field,QWidget) and not isinstance(field,QLabel):label.setBuddy(field)
        return super().addRow(label,field)

def form():
    layout=SettingsForm();layout.setLabelAlignment(Qt.AlignmentFlag.AlignLeft|Qt.AlignmentFlag.AlignVCenter)
    layout.setContentsMargins(0,0,0,0)
    layout.setHorizontalSpacing(24);layout.setVerticalSpacing(14);layout.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
    return layout

class EncoderWindow(QMainWindow):
    def __init__(self):
        super().__init__();self.setWindowTitle('nMedia Encoder');self.resize(1120,860);self.setMinimumSize(820,650)
        self.setWindowIcon(QIcon(str(Path(__file__).resolve().parent/'assets/app.svg')));self.settings=QSettings('nMedia','Encoder')
        self.info=None;self.original_info=None;self.original_path='';self.task=None;self.task_type='';self.task_error=None;self.job_started=0;self.completed_output=''
        self.external_subtitles=[];self.loading_tracks=False;self.selected_path='';self.audio_requested=self.settings.value('include_audio',True,type=bool)
        self.root=QWidget();self.setCentralWidget(self.root)
        main=QVBoxLayout(self.root);main.setContentsMargins(24,18,24,16);main.setSpacing(14)
        header=QHBoxLayout();title=QLabel('nMedia Encoder');title.setObjectName('appTitle');header.addWidget(title)
        header.addStretch();privacy=muted('Files stay on your computer');privacy.setWordWrap(False);header.addWidget(privacy)
        self.copy=QPushButton('Copy command');self.copy.setIcon(icon('copy'));self.copy.setToolTip('Copy the equivalent Python CLI command. Run it from the source repository.');header.addWidget(self.copy)
        main.addLayout(header)
        self.source=SourceCard();main.addWidget(self.source)
        mode_row=QHBoxLayout();mode_row.addWidget(QLabel('Create'))
        self.operation=combo([('Video for nMedia','video'),('Audio file','audio')]);self.operation.setMinimumWidth(220)
        mode_row.addWidget(self.operation);self.operation_hint=muted('Video, with optional audio and subtitles.');mode_row.addWidget(self.operation_hint,1);main.addLayout(mode_row)
        self.edit_hint=muted('The encoded video is copied unchanged. Subtitles burned into the picture cannot be removed.');self.edit_hint.hide();main.addWidget(self.edit_hint)
        self.original_bar=QWidget();original_layout=QGridLayout(self.original_bar);original_layout.setContentsMargins(0,0,0,0);original_layout.setHorizontalSpacing(12);original_layout.setVerticalSpacing(8)
        self.original_entry=QLineEdit();self.original_entry.setReadOnly(True);self.original_entry.setPlaceholderText('Choose the original video or an audio file to add tracks')
        self.original_choose=QPushButton('Choose source…');self.original_choose.clicked.connect(self.choose_original)
        self.original_start=QLineEdit('0');self.original_start.setMaximumWidth(110);self.original_start.setAccessibleName('Encoded video start time in the original source')
        original_layout.addWidget(QLabel('Track source'),0,0);original_layout.addWidget(self.original_entry,0,1);original_layout.addWidget(self.original_choose,0,2)
        original_layout.addWidget(QLabel('Starts in source'),1,0);original_layout.addWidget(horizontal(self.original_start,muted('Use the original start time if the encoded video was trimmed.')),1,1,1,2)
        self.original_bar.hide();main.addWidget(self.original_bar)
        content=QHBoxLayout();content.setSpacing(22)
        left=QFrame();left.setObjectName('previewPanel');left.setMinimumWidth(280)
        left_layout=QVBoxLayout(left);left_layout.setContentsMargins(16,14,16,14);left_layout.setSpacing(9)
        self.preview_title=QLabel('Framing preview');self.preview_title.setObjectName('sectionTitle');left_layout.addWidget(self.preview_title)
        self.preview=Preview();left_layout.addWidget(self.preview)
        self.framing=muted('Choose a video to preview its framing.');left_layout.addWidget(self.framing)
        line=QFrame();line.setObjectName('divider');line.setFrameShape(QFrame.Shape.HLine);left_layout.addWidget(line)
        self.profile=combo([('CX II · 24 FPS','cx2'),('Original CX · 16 FPS','cx')]);self.profile.setAccessibleName('Calculator preset')
        self.summary_title=QLabel('Output');self.summary_title.setObjectName('sectionTitle');left_layout.addWidget(self.summary_title)
        self.summary=muted('H.264 · 320 × 240\nOpus audio · 128 kbps');self.summary.setWordWrap(True);left_layout.addWidget(self.summary)
        self.size_hint=muted('');left_layout.addWidget(self.size_hint);left_layout.addStretch()
        left_scroll=QScrollArea();left_scroll.setWidgetResizable(True);left_scroll.setFixedWidth(304);left_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff);left_scroll.setWidget(left);content.addWidget(left_scroll)
        self.tabs=QTabWidget();self.tabs.setDocumentMode(True);self.tabs.tabBar().setDrawBase(False);content.addWidget(self.tabs,1)
        self._video_tab();self._audio_tab();self._subtitle_tab();main.addLayout(content,1)
        self.error=QLabel();self.error.setObjectName('error');self.error.setTextFormat(Qt.TextFormat.PlainText);self.error.setWordWrap(True);self.error.hide();main.addWidget(self.error)
        output_row=QHBoxLayout();output_row.addWidget(QLabel('Save as'))
        self.output=QLineEdit();self.output.setPlaceholderText('Choose an output file');self.output.setAccessibleName('Output video filename');output_row.addWidget(self.output,1)
        self.output_browse=QPushButton('Browse…');output_row.addWidget(self.output_browse);main.addLayout(output_row)
        self.progress=QProgressBar();self.progress.setRange(0,1000);self.progress.setValue(0);self.progress.setTextVisible(False);self.progress.hide();main.addWidget(self.progress)
        status_row=QHBoxLayout();status_box=QVBoxLayout();status_box.setSpacing(5)
        self.status=QLabel('Choose a video to get started.');self.status.setTextFormat(Qt.TextFormat.PlainText)
        self.status_detail=muted('The command-line encoder is still available.');status_box.addWidget(self.status);status_box.addWidget(self.status_detail);status_row.addLayout(status_box,1)
        self.open_folder=QPushButton('Open folder');self.open_folder.hide();status_row.addWidget(self.open_folder)
        self.cancel=QPushButton('Cancel');self.cancel.hide();status_row.addWidget(self.cancel)
        self.encode=QPushButton('Encode video');self.encode.setObjectName('primary');self.encode.setIcon(icon('play','#25180e'));self.encode.setMinimumWidth(160);status_row.addWidget(self.encode);main.addLayout(status_row)
        log_row=QHBoxLayout();self.details=QToolButton();self.details.setText('Show details');self.details.setCheckable(True)
        self.save_log=QPushButton('Save log…');self.save_log.hide();log_row.addWidget(self.details);log_row.addStretch();log_row.addWidget(self.save_log);main.addLayout(log_row)
        self.log=QPlainTextEdit();self.log.setReadOnly(True);self.log.setMaximumBlockCount(2000);self.log.setMaximumHeight(150);self.log.hide();main.addWidget(self.log)
        self.details.toggled.connect(self.toggle_details);self.save_log.clicked.connect(self.save_details)
        self.source.browse.clicked.connect(self.choose_source);self.source.selected.connect(self.load_source)
        self.output_browse.clicked.connect(self.choose_output);self.copy.clicked.connect(self.copy_command);self.encode.clicked.connect(self.start_encode)
        self.cancel.clicked.connect(self.cancel_task);self.open_folder.clicked.connect(lambda:QDesktopServices.openUrl(QUrl.fromLocalFile(str(Path(self.completed_output).parent))))
        self.profile.currentIndexChanged.connect(self.apply_profile)
        self.operation.currentIndexChanged.connect(self.change_operation)
        self.timer=QTimer(self);self.timer.setInterval(50);self.timer.timeout.connect(self.poll_task);self.timer.start()
        QShortcut(QKeySequence.StandardKey.Open,self,activated=self.choose_source)
        QShortcut(QKeySequence('Ctrl+Return'),self,activated=self.start_encode)
        QShortcut(QKeySequence('Escape'),self,activated=lambda:self.cancel_task() if self.task else None)
        for control in self.findChildren(QWidget):
            if isinstance(control,QLineEdit):control.textChanged.connect(self.update_options)
            elif isinstance(control,QComboBox):control.currentIndexChanged.connect(self.update_options)
            elif isinstance(control,(QSpinBox,QDoubleSpinBox)):control.valueChanged.connect(self.update_options)
            elif isinstance(control,(QCheckBox,QRadioButton)):control.toggled.connect(self.update_options)
        self.update_options()

    def tab_page(self,title):
        scroll=QScrollArea();scroll.setWidgetResizable(True);body=QWidget();scroll.setWidget(body)
        box=QVBoxLayout(body);box.setContentsMargins(22,22,22,22);box.setSpacing(18)
        self.tabs.addTab(scroll,title);return body,box

    def _video_tab(self):
        body,box=self.tab_page('Video')
        fields=form();fields.addRow('Calculator preset',self.profile);self.codec=combo([(label,key) for key,label in VIDEO_CODECS.items()]);fields.addRow('Video codec',self.codec)
        self.codec_hint=muted('H.264 offers a good balance of picture quality and playback speed.');fields.addRow('',self.codec_hint)
        self.fps=combo([('Source frame rate','source'),('12 FPS','12'),('16 FPS','16'),('20 FPS','20'),('24 FPS','24'),('25 FPS','25'),('30 FPS','30')]);self.fps.setCurrentIndex(4);self.fps.setEditable(True)
        fields.addRow('Frame rate',self.fps)
        self.width=spin(16,320,320);self.height=spin(16,240,240);fields.addRow('Fit within',horizontal(self.width,QLabel('×'),self.height,QLabel('pixels')))
        self.hdr=QCheckBox('Convert HDR to SDR');self.hdr.setToolTip('Tone-map HDR brightness and colours for the calculator display. Leave off for SDR sources.');fields.addRow('',self.hdr)
        box.addLayout(fields)
        modes=QHBoxLayout();self.quality_mode=QRadioButton('Choose quality');self.size_mode=QRadioButton('Target file size');self.quality_mode.setChecked(True)
        self.mode_group=QButtonGroup(self);self.mode_group.addButton(self.quality_mode);self.mode_group.addButton(self.size_mode);modes.addWidget(self.quality_mode);modes.addWidget(self.size_mode);modes.addStretch();box.addLayout(modes)
        self.quality_box=QWidget();quality=form();self.quality_box.setLayout(quality)
        self.quality_choice=combo([('Higher quality','high'),('Balanced','balanced'),('Smaller file','small'),('Custom','custom')]);self.quality_choice.setCurrentIndex(1);quality.addRow('Quality',self.quality_choice)
        self.quality_help=muted('Higher quality keeps more detail and uses more space.');quality.addRow('',self.quality_help)
        box.addWidget(self.quality_box)
        self.size_box=QWidget();size=form();self.size_box.setLayout(size)
        self.size_preset=combo([('Calculator · 85 MB',85),('50 MB',50),('25 MB',25),('Custom size',0)]);size.addRow('Size preset',self.size_preset)
        self.target_size=spin(.1,100000,85,1);self.target_size.setSuffix(' MB');size.addRow('Total file size',self.target_size)
        self.two_pass=QCheckBox('Two-pass encoding');self.two_pass.setChecked(True);size.addRow('',self.two_pass)
        size.addRow('',muted('Two passes improve how quality is distributed at a fixed size. Includes video and audio, with 3% reserved for overhead. MB = 1,000,000 bytes. Final size can vary.'));box.addWidget(self.size_box)
        self.size_preset.currentIndexChanged.connect(self.apply_size_preset)
        self.target_size.editingFinished.connect(lambda:self.size_preset.setCurrentIndex(3))
        self.advanced_button=QToolButton();self.advanced_button.setText('Advanced video settings');self.advanced_button.setCheckable(True);self.advanced_button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon);self.advanced_button.setArrowType(Qt.ArrowType.RightArrow);box.addWidget(self.advanced_button)
        self.advanced=QWidget();advanced=form();self.advanced.setLayout(advanced)
        self.quality=spin(0,51,24,1);self.quality_label=QLabel('Quality (CRF)');advanced.addRow(self.quality_label,self.quality)
        self.mpeg_bitrate=spin(16,20000,500);self.mpeg_bitrate.setSuffix(' kbps');self.mpeg_label=QLabel('Video bitrate');advanced.addRow(self.mpeg_label,self.mpeg_bitrate)
        self.preset=combo([(s.capitalize(),s) for s in ('ultrafast','fast','medium','slow','veryslow')]);self.preset.setCurrentIndex(3);advanced.addRow('Encoding effort',self.preset)
        self.stream_profile=combo([('Fast playback','fast'),('Balanced','balanced'),('Higher detail','quality'),('Every frame independent','intra')]);advanced.addRow('Playback profile',self.stream_profile)
        self.stream_profile.setToolTip('Fast prioritizes calculator playback. Higher detail allows more decoding work. Independent frames make much larger files.')
        self.preset.setToolTip('Controls encoding effort on this computer. Slower settings spend more time on compression.')
        self.start=QLineEdit('0');self.end=QLineEdit();self.end.setPlaceholderText('End of video');advanced.addRow('Trim from / to',horizontal(self.start,self.end));self.start.setAccessibleName('Start time');self.end.setAccessibleName('End time')
        self.aspect=combo([('Keep source framing',''),('16:9','16:9'),('4:3','4:3'),('2.39:1','2.39')]);advanced.addRow('Centre crop',self.aspect)
        self.crop=QLineEdit();self.crop.setPlaceholderText('Optional: width:height:x:y');advanced.addRow('Exact crop',self.crop)
        self.idr=combo([('Automatic','auto'),('Measured byte budget (H.264)','byte-auto')]);self.idr.setEditable(True);advanced.addRow('Keyframe spacing',self.idr)
        self.chunk=spin(1,4096,64);self.chunk.setSuffix(' KiB');advanced.addRow('Chunk size',self.chunk)
        self.preview_mp4=QCheckBox('Also save an MP4 picture preview');advanced.addRow('',self.preview_mp4)
        box.addWidget(self.advanced);self.advanced.hide();self.advanced_button.toggled.connect(self.toggle_advanced);box.addStretch()
        self.codec.currentIndexChanged.connect(self.change_codec)
        self.quality_choice.currentIndexChanged.connect(self.apply_quality)
        self.quality.editingFinished.connect(lambda:self.quality_choice.setCurrentIndex(3));self.mpeg_bitrate.editingFinished.connect(lambda:self.quality_choice.setCurrentIndex(3))

    def _audio_tab(self):
        body,box=self.tab_page('Audio');self.audio_enabled=QCheckBox('Include audio');self.audio_enabled.setChecked(self.audio_requested);self.audio_enabled.clicked.connect(self.remember_audio);box.addWidget(self.audio_enabled)
        self.audio_edit=QWidget();edit_fields=form();self.audio_edit.setLayout(edit_fields);self.audio_current=muted('');self.audio_action=combo([('Keep current soundtrack','keep'),('Remove soundtrack','remove'),('Add / replace from source','replace')]);edit_fields.addRow('Soundtrack',self.audio_action);edit_fields.addRow('',self.audio_current);self.audio_edit.hide();box.addWidget(self.audio_edit)
        self.audio_notice=muted('Sound plays through compatible USB headphones, speakers or an audio adapter connected to the calculator.');box.addWidget(self.audio_notice)
        self.music_preset_bar=QWidget();preset_form=form();self.music_preset_bar.setLayout(preset_form)
        self.music_preset=combo([('Opus · 128 kbps · compact music',('opus',128)),('MP3 · 192 kbps · broad compatibility',('mp3',192)),('AAC · 128 kbps',('aac',128)),('Vorbis · 160 kbps',('vorbis',160)),('FLAC · lossless',('flac',128)),('Custom settings',None)])
        preset_form.addRow('Music preset',self.music_preset);box.addWidget(self.music_preset_bar);self.music_preset_bar.hide()
        self.audio_fields=QWidget();fields=form();self.audio_form=fields;self.audio_fields.setLayout(fields)
        self.audio_track=QComboBox();self.audio_track.setMinimumContentsLength(20);self.audio_track.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon);fields.addRow('Source track',self.audio_track)
        self.audio_codec=combo([(name,key) for key,name in AUDIO_CODECS.items()]);fields.addRow('Audio codec',self.audio_codec)
        self.audio_hint=muted('Opus keeps good quality at small sizes.');fields.addRow('',self.audio_hint)
        self.audio_bitrate=combo([(f'{n} kbps',n) for n in (32,48,64,96,128,160,192,256,320)]);self.audio_bitrate.setCurrentIndex(4);fields.addRow('Audio bitrate',self.audio_bitrate)
        self.audio_rate=combo([('Source',0)]+[(f'{n:,} Hz',n) for n in (8000,16000,22050,32000,44100,48000,96000)]);self.audio_rate.setCurrentIndex(6);fields.addRow('Sample rate',self.audio_rate)
        self.audio_channels=combo([('Stereo',2),('Mono',1),('Source (up to stereo)',0)]);fields.addRow('Channels',self.audio_channels);box.addWidget(self.audio_fields)
        self.music_box=QWidget();music=QVBoxLayout(self.music_box);music.setContentsMargins(0,0,0,0);music.setSpacing(14)
        self.music_size=QCheckBox('Target a final file size');music.addWidget(self.music_size)
        music_form=form();self.music_size_form=music_form;self.music_target=spin(.01,100000,5,2);self.music_target.setSuffix(' MB');music_form.addRow('Total file size',self.music_target);music.addLayout(music_form)
        self.music_size_help=muted('Includes tags and embedded art. The bitrate is calculated from the duration; audio does not need a video-style two-pass encode.');music.addWidget(self.music_size_help)
        self.keep_tags=QCheckBox('Keep tags');self.keep_tags.setChecked(True);self.keep_tags.setToolTip('Keep title, artist, album, track/disc numbers, date, genre, comments, lyrics and additional text tags.')
        self.keep_art=QCheckBox('Keep embedded album art');self.keep_art.setChecked(True);music.addWidget(horizontal(self.keep_tags,self.keep_art))
        self.music_metadata=muted('');music.addWidget(self.music_metadata)
        self.music_tns=QCheckBox('Add .tns for calculator storage');music.addWidget(self.music_tns)
        self.music_advanced=QToolButton();self.music_advanced.setText('Advanced audio settings');self.music_advanced.setCheckable(True);self.music_advanced.setArrowType(Qt.ArrowType.RightArrow);self.music_advanced.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon);music.addWidget(self.music_advanced)
        self.music_trim=QWidget();trim=form();self.music_trim.setLayout(trim);self.music_start=QLineEdit('0');self.music_end=QLineEdit();self.music_end.setPlaceholderText('End of track');trim.addRow('Trim from / to',horizontal(self.music_start,self.music_end));music.addWidget(self.music_trim)
        box.addWidget(self.music_box);self.music_box.hide();box.addStretch()
        self.music_preset.currentIndexChanged.connect(self.apply_music_preset)
        self.audio_codec.currentIndexChanged.connect(self.update_audio_filename)
        self.music_tns.toggled.connect(self.update_audio_filename)
        self.music_size.toggled.connect(lambda on:self.music_preset.setCurrentIndex(5) if on else None)
        self.music_advanced.toggled.connect(self.update_options)
        self.audio_codec.activated.connect(lambda:self.music_preset.setCurrentIndex(5))
        self.audio_bitrate.activated.connect(lambda:self.music_preset.setCurrentIndex(5))

    def apply_size_preset(self):
        value=self.size_preset.currentData()
        if value:self.target_size.setValue(value)

    def apply_music_preset(self):
        value=self.music_preset.currentData()
        if value:
            self.audio_codec.setCurrentIndex(self.audio_codec.findData(value[0]));self.audio_bitrate.setCurrentIndex(self.audio_bitrate.findData(value[1]))
            self.music_size.setChecked(False)

    def is_audio(self):return self.operation.currentData()=='audio' and not (self.info or {}).get('encoded')

    def update_audio_filename(self):
        if not hasattr(self,'output') or not self.is_audio() or not self.output.text():return
        value=self.output.text()
        if value.lower().endswith('.nvp.tns'):value=value[:-8]
        elif value.lower().endswith('.tns'):value=value[:-4]
        value=audio_path(value,self.audio_codec.currentData())
        if self.selected_path and Path(value).resolve()==Path(self.selected_path).resolve():
            path=Path(value);value=str(path.with_name(path.stem+'.converted'+path.suffix))
        if self.music_tns.isChecked():value+='.tns'
        if value!=self.output.text():self.output.setText(value)

    def change_operation(self):
        if not hasattr(self,'output'):return
        audio=self.is_audio()
        self.tabs.widget(0).setEnabled(not audio and not (self.info or {}).get('encoded'))
        self.tabs.widget(2).setEnabled(not audio)
        if audio:
            self.tabs.setCurrentIndex(1);self.audio_rate.setCurrentIndex(0);self.audio_channels.setCurrentIndex(2);self.update_audio_filename()
        else:
            if not self.audio_rate.currentData():self.audio_rate.setCurrentIndex(6)
            if not self.audio_channels.currentData():self.audio_channels.setCurrentIndex(0)
            if self.selected_path and not (self.info or {}).get('encoded'):self.output.setText(str(Path(self.selected_path).with_suffix('.nvp.tns')))
        self.update_options()

    def _subtitle_tab(self):
        body,box=self.tab_page('Subtitles');fields=form()
        self.subtitle_mode=combo([('No subtitles','none'),('Selectable on the calculator','embedded'),('Burn into the picture','burn')]);fields.addRow('Subtitle mode',self.subtitle_mode);box.addLayout(fields)
        self.subtitle_hint=muted('Selectable subtitles can be changed or turned off during playback. Image-based subtitles need to be burned in.');box.addWidget(self.subtitle_hint)
        self.tracks=QTreeWidget();self.tracks.setHeaderLabels(['Subtitle track','Type']);self.tracks.setRootIsDecorated(False);self.tracks.setMinimumHeight(150);self.tracks.header().setSectionResizeMode(0,QHeaderView.ResizeMode.Stretch);self.tracks.header().setSectionResizeMode(1,QHeaderView.ResizeMode.ResizeToContents)
        self.tracks.itemChanged.connect(self.subtitle_changed);box.addWidget(self.tracks,1)
        row=QHBoxLayout();self.add_subtitle=QPushButton('Add subtitle file…');self.remove_subtitle=QPushButton('Remove file');row.addWidget(self.add_subtitle);row.addWidget(self.remove_subtitle);row.addStretch();box.addLayout(row)
        self.add_subtitle.clicked.connect(self.choose_subtitle);self.remove_subtitle.clicked.connect(self.remove_external)
        self.remove_subtitle.setEnabled(False);self.tracks.itemSelectionChanged.connect(lambda:self.remove_subtitle.setEnabled(any(item.data(0,Qt.ItemDataRole.UserRole)[0]=='external' for item in self.tracks.selectedItems())))
        self.burn_size=spin(.25,4,1,2);fields=form();self.burn_form=fields;fields.addRow('Burned text size',self.burn_size);box.addLayout(fields)

    def toggle_advanced(self,on):
        self.advanced.setVisible(on);self.advanced_button.setArrowType(Qt.ArrowType.DownArrow if on else Qt.ArrowType.RightArrow)
    def remember_audio(self,on):
        self.audio_requested=on;self.settings.setValue('include_audio',on)
    def toggle_details(self,on):
        self.log.setVisible(on);self.save_log.setVisible(on);self.details.setText('Hide details' if on else 'Show details')
    def change_codec(self):
        key=self.codec.currentData();self.quality.setMaximum(63 if key=='av1' else 51);self.quality.setDecimals(0 if key=='av1' else 1)
        previous=self.stream_profile.currentData()
        choices=[('Fast playback','fast'),('Balanced','balanced'),('Higher detail','quality'),('Every frame independent','intra')]
        if key=='mpeg4':choices=[choices[0],choices[3]]
        elif key=='av1':choices=[choices[0],choices[2],choices[3]]
        self.stream_profile.blockSignals(True);self.stream_profile.clear()
        for label,value in choices:self.stream_profile.addItem(label,value)
        self.stream_profile.setCurrentIndex(next((i for i,(_,value) in enumerate(choices) if value==previous),0));self.stream_profile.blockSignals(False)
        self.preset.setEnabled(key!='mpeg4')
        self.preset.setToolTip('MPEG-4 uses fixed encoder settings.' if key=='mpeg4' else 'Controls encoding effort on this computer. Slower settings spend more time on compression.')
        self.apply_quality();self.update_options()
    def apply_quality(self):
        choice=self.quality_choice.currentData();key=self.codec.currentData()
        if choice=='custom':
            self.advanced_button.setChecked(True);return
        index={'high':0,'balanced':1,'small':2}[choice]
        self.quality.setValue({'h264':(18,24,30),'hevc':(23,28,33),'av1':(32,40,48),'mpeg4':(18,24,30)}[key][index])
        self.mpeg_bitrate.setValue((750,500,300)[index]);self.update_options()
    def apply_profile(self):
        self.fps.setCurrentIndex(4 if self.profile.currentData()=='cx2' else 2);self.codec.setCurrentIndex(0);self.width.setValue(320);self.height.setValue(240)
    def options(self):
        fps=self.fps.currentData() if self.fps.currentIndex()>=0 and self.fps.currentText()==self.fps.itemText(self.fps.currentIndex()) else self.fps.currentText().strip().lower().replace('fps','').strip()
        idr=self.idr.currentData() if self.idr.currentIndex()>=0 and self.idr.currentText()==self.idr.itemText(self.idr.currentIndex()) else self.idr.currentText().strip()
        tracks=[];external=[];keep=[];source_tracks=[]
        for i in range(self.tracks.topLevelItemCount()):
            item=self.tracks.topLevelItem(i)
            if item.checkState(0)==Qt.CheckState.Checked:
                kind,value=item.data(0,Qt.ItemDataRole.UserRole)
                if kind=='embedded':tracks.append(value)
                elif kind=='existing':keep.append(value)
                elif kind=='source':source_tracks.append(value)
                else:external.append(value)
        fields=dict(source=self.selected_path,output=self.output.text(),codec=self.codec.currentData(),fps=str(fps),
            width=self.width.value(),height=self.height.value(),crf=self.quality.value(),rate_mode='size' if self.size_mode.isChecked() else 'quality',
            target_mb=self.target_size.value(),two_pass=self.two_pass.isChecked(),video_kbps=self.mpeg_bitrate.value(),preset=self.preset.currentData(),stream_profile=self.stream_profile.currentData(),hdr=self.hdr.isChecked(),
            audio=self.audio_enabled.isChecked(),audio_track=self.audio_track.currentData() if self.audio_track.currentData() is not None else -1,
            audio_codec=self.audio_codec.currentData(),audio_bitrate=self.audio_bitrate.currentData(),audio_rate=self.audio_rate.currentData(),audio_channels=self.audio_channels.currentData(),
            subtitle_mode=self.subtitle_mode.currentData(),subtitle_tracks=tracks,external_subtitles=external,subtitle_size=self.burn_size.value(),start=self.start.text(),end=self.end.text(),
            crop=self.crop.text().strip(),active_aspect=self.aspect.currentData(),idr=str(idr),chunk_kib=self.chunk.value(),preview=self.preview_mp4.isChecked())
        if self.is_audio():
            fields.update(rate_mode='size' if self.music_size.isChecked() else 'quality',target_mb=self.music_target.value(),
                start=self.music_start.text(),end=self.music_end.text(),keep_tags=self.keep_tags.isChecked(),keep_art=self.keep_art.isChecked())
            return AudioOptions(**fields)
        if (self.info or {}).get('encoded'):
            return TrackEditOptions(**fields,track_source=self.original_path,source_start=self.original_start.text(),audio_action=self.audio_action.currentData(),
                keep_subtitle_tracks=keep,source_subtitle_tracks=source_tracks,original_info=self.original_info)
        return EncodeOptions(**fields)

    def update_options(self,*args):
        if not hasattr(self,'encode'):return
        options=self.options();busy=bool(self.task)
        self.quality_box.setVisible(not self.size_mode.isChecked());self.size_box.setVisible(self.size_mode.isChecked())
        mpeg=options.codec=='mpeg4';quality_mode=options.rate_mode=='quality';self.mpeg_bitrate.setVisible(mpeg and quality_mode);self.mpeg_label.setVisible(mpeg and quality_mode);self.quality.setVisible(not mpeg and quality_mode);self.quality_label.setVisible(not mpeg and quality_mode)
        editing=bool((self.info or {}).get('encoded'));music=self.is_audio()
        self.music_box.setVisible(music);self.audio_enabled.setVisible(not editing and not music)
        self.music_preset_bar.setVisible(music)
        self.operation.setEnabled(not busy and not editing and not (self.info or {}).get('audio_only'))
        self.audio_notice.setVisible(not music)
        self.operation_hint.setText('Ordinary music files, with tags and embedded album art.' if music else 'Edit tracks without re-encoding the picture.' if editing else 'Video, with optional audio and subtitles.')
        self.preview_title.setText('Album art' if music else 'Framing preview')
        self.preview.audio=music
        if music:
            metadata=(self.info or {}).get('music',{})
            tags=metadata.get('tags',{})
            self.framing.setText('\n'.join(', '.join(tags.get(key,[])) for key in ('TITLE','ARTIST','ALBUM') if tags.get(key)) or 'Embedded album art is kept when available.')
            self.music_metadata.setText(f"{len(tags)} tags · {metadata.get('covers',0)} embedded cover(s) · {size_text(metadata.get('cover_bytes',0))} of art" if metadata else 'Standard source metadata is kept where the output format supports it.')
        self.music_target.setEnabled(self.music_size.isChecked());self.music_size_help.setVisible(self.music_size.isChecked())
        self.music_size_form.setRowVisible(self.music_target,self.music_size.isChecked())
        self.audio_form.setRowVisible(self.audio_track,not music or len((self.info or {}).get('audio',[]))>1)
        self.audio_form.setRowVisible(self.audio_bitrate,not music or not self.music_size.isChecked())
        self.audio_form.setRowVisible(self.audio_rate,not music or self.music_advanced.isChecked())
        self.audio_form.setRowVisible(self.audio_channels,not music or self.music_advanced.isChecked())
        self.music_trim.setVisible(self.music_advanced.isChecked());self.music_advanced.setArrowType(Qt.ArrowType.DownArrow if self.music_advanced.isChecked() else Qt.ArrowType.RightArrow)
        self.audio_bitrate.setToolTip('Calculated from the size target.' if music and self.music_size.isChecked() else 'Audio bitrate in kilobits per second.')
        self.audio_fields.setEnabled((self.audio_action.currentData()=='replace' and bool((self.original_info or {}).get('audio'))) if editing else (music or self.audio_enabled.isChecked()) and bool((self.info or {}).get('audio')))
        compressed=options.audio_codec in ('opus','mp3','aac','vorbis');self.audio_bitrate.setEnabled(compressed and not (music and self.music_size.isChecked()))
        # Source is meaningful for ordinary music conversion; NVP soundtracks
        # require explicit rate/channel values supported by the calculator.
        self.audio_rate.model().item(0).setEnabled(music);self.audio_channels.model().item(2).setEnabled(music)
        self.audio_rate.setEnabled(options.audio_codec!='opus');self.burn_size.setEnabled(options.subtitle_mode=='burn')
        self.burn_form.setRowVisible(self.burn_size,not editing and options.subtitle_mode=='burn')
        self.audio_track.setToolTip(self.audio_track.currentText())
        self.tracks.setEnabled(options.subtitle_mode!='none')
        self.audio_hint.setText({'opus':'Good quality at small sizes. Opus uses 48 kHz.', 'mp3':'Broad compatibility and efficient playback.', 'ima':'Low decoding cost, with larger files than Opus or MP3.', 'pcm':'Uncompressed audio. Large files, little decoding work.', 'flac':'Lossless audio. Size depends on the recording.', 'aac':'Compact audio with broad support.', 'vorbis':'Good quality at moderate bitrates.'}[options.audio_codec])
        self.codec_hint.setText({'h264':'A good balance of picture quality and calculator playback speed.', 'mpeg4':'A lightweight video decoder. Uses a video bitrate rather than CRF.', 'hevc':'Smaller files can require more calculator CPU to decode.', 'av1':'Efficient compression, with a higher decoding cost on the calculator.'}[options.codec])
        errors=options.errors(self.info)
        show_error=bool(self.selected_path) and self.info and errors and not busy
        self.error.setVisible(bool(show_error))
        if show_error:self.error.setText(next(iter(errors.values())))
        for control,key in ((self.output,'output'),(self.fps,'fps'),(self.width,'dimensions'),(self.height,'dimensions'),
            (self.target_size,'size'),(self.start,'trim'),(self.end,'trim'),(self.audio_track,'audio'),
            (self.subtitle_mode,'subtitles'),(self.crop,'advanced'),(self.idr,'advanced'),(self.original_start,'trim'),
            (self.music_target,'size'),(self.music_start,'trim'),(self.music_end,'trim')):
            invalid=bool(show_error and key in errors)
            if control.property('invalid')!=invalid:
                control.setProperty('invalid',invalid);control.style().unpolish(control);control.style().polish(control)
        self.encode.setEnabled(not busy and not errors);self.copy.setEnabled(not errors)
        subtitle_count=(len(options.keep_subtitle_tracks)+len(options.source_subtitle_tracks)+len(options.external_subtitles) if editing else len(options.subtitle_tracks)+len(options.external_subtitles)) if options.subtitle_mode!='none' else 0
        audio=f"{AUDIO_CODECS[options.audio_codec]} · {options.audio_bitrate} kbps" if options.audio and compressed else AUDIO_CODECS[options.audio_codec] if options.audio else 'No audio'
        if editing:audio='Keep current soundtrack' if options.audio_action=='keep' and self.info['audio'] else 'No audio' if options.audio_action!='replace' else f'{AUDIO_CODECS[options.audio_codec]} · {options.audio_bitrate} kbps'
        self.summary.setText(f"{VIDEO_CODECS[options.codec]} · {'Source FPS' if options.fps=='source' else options.fps+' FPS'}\n{audio}\n{subtitle_count} subtitle track{'s' if subtitle_count!=1 else ''}")
        if music:
            self.summary.setText(f"{AUDIO_CODECS[options.audio_codec]} · {EXTENSIONS[options.audio_codec]}\n"+('Bitrate from size target' if options.rate_mode=='size' else f'{options.audio_bitrate} kbps' if compressed else 'Lossless compression' if options.audio_codec=='flac' else '16-bit uncompressed audio' if options.audio_codec=='pcm' else '4-bit ADPCM audio'))
            if self.info and 'trim' not in errors:
                if options.rate_mode=='size':self.size_hint.setText(f"Limit: {options.target_mb:g} MB\nAudio budget: {max(0,options.budget_kbps(self.info))} kbps\nIncludes tags and album art.")
                else:
                    estimate=options.clip_duration(self.info)*(options.audio_bitrate if compressed else (options.audio_rate or self.info.get('music',{}).get('rate',48000))*(options.audio_channels or 2)*(16 if options.audio_codec=='pcm' else 4)/1000)*125+options.metadata_overhead(self.info)
                    self.size_hint.setText('Final size depends on the recording.' if options.audio_codec=='flac' else 'Estimated size: '+size_text(estimate))
            else:self.size_hint.setText('')
        elif editing:self.size_hint.setText('Video copied unchanged. Only the selected tracks are changed.')
        elif options.rate_mode=='size' and self.info and 'trim' not in errors:
            self.size_hint.setText(f"Target: {options.target_mb:g} MB\nVideo budget: {max(0,options.budget_kbps(self.info)):,} kbps\n{'Two passes' if options.two_pass else 'Single pass'} · {options.preset} encoding")
        else:self.size_hint.setText('Final size depends on the picture and selected quality.')
        self.preview.fit=(options.width,options.height);self.preview.crop='' if editing else options.crop;self.preview.aspect='' if editing else options.active_aspect;self.preview.update()
        if not busy:self.encode.setText('Convert audio' if music else 'Save edited video' if editing else 'Encode video')
        if not busy and not self.completed_output:self.status.setText(('Ready to convert audio.' if music else 'Ready to save track changes.' if editing else 'Ready to encode.') if self.info and not errors else 'Choose a video or music file to get started.' if not self.selected_path else 'Check the highlighted settings.' if errors and self.info else 'Reading media details…')

    def choose_source(self):
        if self.task:return
        path,_=QFileDialog.getOpenFileName(self,'Choose a video or music file',self.settings.value('last_directory',''),'Media files (*.mkv *.mp4 *.mov *.avi *.webm *.m4v *.ts *.m2ts *.nut *.nvp.tns *.mp3 *.flac *.wav *.ogg *.oga *.opus *.m4a *.aac *.aiff *.aif *.wma *.ape *.wv);;All files (*)')
        if path:self.load_source(path)
    def load_source(self,path):
        if self.task:return
        self.selected_path=str(Path(path).resolve());self.info=None;self.original_info=None;self.original_path='';self.original_entry.clear();self.original_start.setText('0');self.completed_output='';self.open_folder.hide();self.log.clear();self.error.hide()
        self.source.title.setText(Path(path).name);self.source.detail.setText('Reading video details…');self.source.browse.setText('Change…');self.source.setToolTip(self.selected_path)
        self.preview.set_image(QPixmap());self.source.set_image(QPixmap());self.output.setText(str(Path(path).with_suffix('.nvp.tns')))
        self.settings.setValue('last_directory',str(Path(path).parent))
        self.task_type='probe';self.task_error=None
        try:self.task=Task({'task':'probe','path':self.selected_path,'allow_audio':True})
        except OSError as error:
            self.error.setText(f'Could not read this video: {error}');self.error.show();return
        self.set_busy(True)
        self.status.setText('Reading media details…');self.status_detail.setText('You can cancel if this file is unavailable.');self.progress.setRange(0,0)
    def set_source_info(self,info):
        self.info=info;self.source.detail.setText(f"{duration_text(info['duration'])}  ·  "+(f"{info['width']} × {info['height']}  ·  {info['fps']:.3g} FPS  ·  " if info['width'] else 'Audio file  ·  ')+size_text(info['bytes']))
        self.framing.setText(f"{info.get('video_codec') or 'Video'}{' · HDR source' if info['hdr'] else ' · SDR source'}\nSubtitles appear in the encoded file.")
        self.preview.source_size=(info['width'],info['height'])
        editing=bool(info.get('encoded'));self.edit_hint.setVisible(editing);self.original_bar.setVisible(editing);self.audio_enabled.setVisible(not editing);self.audio_edit.setVisible(editing);self.tabs.widget(0).setEnabled(not editing)
        self.subtitle_mode.blockSignals(True);self.subtitle_mode.clear()
        if editing:self.subtitle_mode.addItem('Select tracks to keep or add','embedded');self.subtitle_mode.addItem('Remove all selectable subtitles','none')
        else:
            for label,key in [('No subtitles','none'),('Selectable on the calculator','embedded'),('Burn into the picture','burn')]:self.subtitle_mode.addItem(label,key)
        self.subtitle_mode.blockSignals(False)
        if editing:
            self.codec.setCurrentIndex(list(VIDEO_CODECS).index(info['video_codec'].lower()));self.fps.setEditText(f"{info['fps']:.6g}");self.width.setValue(info['width']);self.height.setValue(info['height'])
            self.output.setText(str(Path(self.selected_path).with_name(Path(self.selected_path).name[:-8]+'.edited.nvp.tns')))
            current=info['audio'][0] if info['audio'] else None
            self.audio_current.setText('Current: '+current['codec'].upper()+' · '+current['details'] if current else 'This video has no soundtrack.')
            self.audio_action.setCurrentIndex(0 if info['audio'] else 1);self.tabs.setCurrentIndex(1)
        self.hdr.setChecked(info['hdr']);self.audio_track.clear()
        for track in info['audio']:self.audio_track.addItem(track_label(track),track['ordinal'])
        default=next((i for i,t in enumerate(info['audio']) if t.get('default')),0);self.audio_track.setCurrentIndex(default)
        self.audio_enabled.setChecked(bool(info['audio']) and self.audio_requested);self.audio_enabled.setEnabled(bool(info['audio']))
        self.loading_tracks=True;self.tracks.clear();self.external_subtitles=[]
        for track in info['subtitles']:
            title=track.get('title') or f"Track {track['ordinal']+1}"
            prefix=title[:3].lower()
            if prefix in LANGUAGES:title=LANGUAGES[prefix]+title[3:]
            item=QTreeWidgetItem([title, 'Text' if track['text_supported'] else 'Image'])
            item.setFlags(item.flags()|Qt.ItemFlag.ItemIsUserCheckable);item.setCheckState(0,Qt.CheckState.Checked if editing else Qt.CheckState.Unchecked);item.setData(0,Qt.ItemDataRole.UserRole,('existing' if editing else 'embedded',track['ordinal']));self.tracks.addTopLevelItem(item)
        self.loading_tracks=False;self.subtitle_mode.setCurrentIndex(0);self.update_options()
        self.operation.setCurrentIndex(1 if info.get('audio_only') or not info['width'] else 0)
        self.change_operation()
        if not info['subtitles']:self.subtitle_hint.setText('No embedded subtitles were found. You can add an SRT, ASS or SSA subtitle file.')
        elif editing:self.subtitle_hint.setText('Keep the checked current tracks, add text tracks from the original, or add subtitle files. Burned-in subtitles remain part of the picture.')
        else:self.subtitle_hint.setText('Selectable subtitles can be changed or turned off during playback. Image-based subtitles need to be burned in.')
    def choose_original(self):
        if self.task:return
        path,_=QFileDialog.getOpenFileName(self,'Choose the original video or track source',self.settings.value('last_directory',''),'Media files (*.mkv *.mp4 *.mov *.avi *.webm *.mp3 *.flac *.wav *.ogg *.opus *.m4a *.aac);;All files (*)')
        if not path:return
        self.original_path=str(Path(path).resolve());self.original_info=None;self.original_entry.setText(path);self.original_entry.setToolTip(path)
        self.task=Task({'task':'probe','path':path,'allow_audio':True});self.task_type='original';self.task_error=None;self.set_busy(True);self.progress.setRange(0,0);self.status.setText('Reading track source…')
    def set_original_info(self,info):
        self.original_info=info;self.audio_track.clear()
        for track in info['audio']:self.audio_track.addItem(track_label(track),track['ordinal'])
        self.audio_track.setCurrentIndex(next((i for i,t in enumerate(info['audio']) if t.get('default')),0))
        self.loading_tracks=True
        for i in reversed(range(self.tracks.topLevelItemCount())):
            if self.tracks.topLevelItem(i).data(0,Qt.ItemDataRole.UserRole)[0]=='source':self.tracks.takeTopLevelItem(i)
        for track in info['subtitles']:
            item=QTreeWidgetItem([track.get('title') or f"Source track {track['ordinal']+1}",'Source · Text' if track['text_supported'] else 'Source · Image'])
            item.setCheckState(0,Qt.CheckState.Unchecked);item.setData(0,Qt.ItemDataRole.UserRole,('source',track['ordinal']));self.tracks.addTopLevelItem(item)
            if not track['text_supported']:
                item.setFlags(item.flags()&~Qt.ItemFlag.ItemIsEnabled);item.setToolTip(0,'Image subtitles must be burned in while converting the original video.')
        self.loading_tracks=False
        if not self.info['audio'] and info['audio']:self.audio_action.setCurrentIndex(2)
        self.update_options()
    def choose_output(self):
        path,_=QFileDialog.getSaveFileName(self,'Save converted audio' if self.is_audio() else 'Save encoded video',self.output.text(),f'Audio (*{EXTENSIONS[self.audio_codec.currentData()]} *{EXTENSIONS[self.audio_codec.currentData()]}.tns)' if self.is_audio() else 'nMedia video (*.nvp.tns)')
        if path:self.output.setText(audio_path(path,self.audio_codec.currentData()) if self.is_audio() else output_path(path))
    def choose_subtitle(self):
        paths,_=QFileDialog.getOpenFileNames(self,'Choose subtitle files',self.settings.value('last_directory',''),'Text subtitles (*.srt *.ass *.ssa);;All files (*)')
        if paths and self.subtitle_mode.currentData()=='none':self.subtitle_mode.setCurrentIndex(1)
        for path in paths:
            if path in self.external_subtitles:continue
            self.external_subtitles.append(path);item=QTreeWidgetItem([Path(path).name,'File']);item.setData(0,Qt.ItemDataRole.UserRole,('external',path));item.setCheckState(0,Qt.CheckState.Checked);item.setToolTip(0,path);self.tracks.addTopLevelItem(item)
        self.update_options()
    def remove_external(self):
        for item in self.tracks.selectedItems():
            kind,value=item.data(0,Qt.ItemDataRole.UserRole)
            if kind=='external':self.external_subtitles.remove(value);self.tracks.takeTopLevelItem(self.tracks.indexOfTopLevelItem(item))
        self.update_options()
    def subtitle_changed(self,item,column):
        if self.loading_tracks:return
        if self.subtitle_mode.currentData()=='burn' and item.checkState(0)==Qt.CheckState.Checked:
            self.loading_tracks=True
            for i in range(self.tracks.topLevelItemCount()):
                other=self.tracks.topLevelItem(i)
                if other is not item:other.setCheckState(0,Qt.CheckState.Unchecked)
            self.loading_tracks=False
        self.update_options()
    def copy_command(self):
        try:QApplication.clipboard().setText(cli_command(self.options().arguments(self.info),bool((self.info or {}).get('encoded')),self.is_audio()))
        except ValueError:return
        self.copy.setText('Copied');QTimer.singleShot(1500,lambda:self.copy.setText('Copy command'))
    def set_busy(self,busy):
        self.source.setAcceptDrops(not busy)
        self.original_bar.setEnabled(not busy)
        self.tabs.setEnabled(not busy);self.source.browse.setEnabled(not busy);self.output.setEnabled(not busy);self.output_browse.setEnabled(not busy);self.profile.setEnabled(not busy)
        self.cancel.setVisible(busy);self.progress.setVisible(busy);self.update_options()
    def start_encode(self):
        if self.task or not self.info:return
        options=self.options()
        try:arguments=options.arguments(self.info)
        except ValueError as error:self.error.setText(str(error));self.error.show();return
        destination=Path(audio_path(options.output,options.audio_codec) if self.is_audio() else output_path(options.output)).resolve()
        if destination.exists():
            answer=QMessageBox.question(self,'Replace existing file?',f'{destination.name} already exists. Replace it when encoding finishes?',QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No,QMessageBox.StandardButton.No)
            if answer!=QMessageBox.StandardButton.Yes:return
        editing=bool(self.info.get('encoded'))
        try:self.task=encode_task(arguments,edit=editing,audio=self.is_audio())
        except (OSError,ValueError) as error:self.error.setText(f'Could not start encoding: {error}');self.error.show();return
        self.output.setText(str(destination));self.task_type='audio' if self.is_audio() else 'edit' if editing else 'encode';self.task_error=None;self.job_started=time.monotonic();self.completed_output='';self.open_folder.hide();self.log.clear();self.error.hide();self.set_busy(True)
        self.progress.setRange(0,0);self.status.setText('Preparing audio…' if self.is_audio() else 'Preparing video…');self.status_detail.setText('The source file stays unchanged.');self.encode.setText('Encoding…')
    def cancel_task(self):
        if self.task:
            self.task.cancel();self.cancel.setEnabled(False);self.status.setText('Stopping…');self.status_detail.setText('Waiting for the encoder to stop before removing temporary files.')
    def handle_messages(self,events):
        for event in events:
            kind=event['kind']
            if kind=='source':self.set_original_info(event['info']) if self.task_type=='original' else self.set_source_info(event['info'])
            elif kind=='preview':
                if self.task_type=='original':continue
                pixmap=QPixmap();pixmap.loadFromData(event['image']);self.preview.set_image(pixmap);self.source.set_image(pixmap)
            elif kind=='log':self.log.appendPlainText(event['text'])
            elif kind=='error':self.task_error=event;self.log.appendPlainText(event['detail'])
            elif kind=='progress':
                if self.task.cancelled:continue
                ratio=event.get('ratio');self.status.setText(event['stage']+(f' · {ratio*100:.0f}%' if ratio is not None else '…'))
                if ratio is None:self.progress.setRange(0,0)
                else:self.progress.setRange(0,1000);self.progress.setValue(int(ratio*1000))
                elapsed=duration_text(time.monotonic()-self.job_started);self.status_detail.setText(f"Elapsed {elapsed}"+(f" · {event['speed']} encoding speed" if event.get('speed') else ''))
    def poll_task(self):
        if not self.task:return
        self.handle_messages(self.task.messages())
        if self.task.process.is_alive():return
        # Drain the final records written just before the process exited.
        self.handle_messages(self.task.messages())
        task=self.task;result=task.result;cancelled=task.cancelled
        try:task.close()
        except OSError as error:self.log.appendPlainText(f'Temporary file cleanup: {error}')
        self.task=None;self.set_busy(False);self.cancel.setEnabled(True);self.progress.hide()
        if result and self.task_type=='audio':
            self.completed_output=result['output_path'];self.open_folder.show();self.status.setText('Audio ready.');self.status_detail.setText(f"{size_text(result['bytes_written'])} · {duration_text(result['encoded_duration'])} · {result['audio_codec'].upper()} · {result['tags']} tags · {result['covers']} cover(s)")
        elif result and self.task_type in ('encode','edit'):
            self.completed_output=result['output_path'];self.open_folder.show();self.status.setText('Video ready.');self.status_detail.setText(f"{size_text(result['bytes_written'])} · {duration_text(result['encoded_duration'])} · {result['width']} × {result['height']} · {result['fps']:.3g} FPS")
        elif cancelled:
            self.status.setText('Cancelled.');self.status_detail.setText('Your source and any existing output were kept.')
        elif self.task_error or not result:
            message=(self.task_error or {}).get('message','The encoder stopped unexpectedly. Open details for the error.')
            self.error.setText(message[:700]);self.error.show();self.status.setText('Could not read this source.' if self.task_type in ('probe','original') else 'Encoding stopped.');self.status_detail.setText('Your source file was not changed. Show details to inspect the error.')
        else:self.status.setText('Ready to save track changes.' if (self.info or {}).get('encoded') else 'Ready to encode.');self.status_detail.setText('Choose your tracks, then save a new copy.' if (self.info or {}).get('encoded') else 'Choose your settings, then encode the video.')
    def save_details(self):
        path,_=QFileDialog.getSaveFileName(self,'Save encoder log','nMedia-encoder-log.txt','Text file (*.txt)')
        if path:
            try:Path(path).write_text(self.log.toPlainText(),encoding='utf8')
            except OSError as error:QMessageBox.warning(self,'Could not save log',str(error))
    def closeEvent(self,event):
        if self.task:
            if QMessageBox.question(self,'Stop and close?','An operation is running. Stop it and close the encoder?',QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No,QMessageBox.StandardButton.No)!=QMessageBox.StandardButton.Yes:event.ignore();return
            self.task.cancel();self.task.process.join(timeout=3)
            if self.task.process.is_alive():event.ignore();return
            self.task.close();self.task=None
        event.accept()

def apply_style(app):
    if not app.property('nmedia-font-loaded'):
        font=Path(__file__).resolve().parent/'assets/PublicSans.ttf'
        if font.is_file():QFontDatabase.addApplicationFont(str(font))
        app.setProperty('nmedia-font-loaded',True)
    app.setApplicationName('nMedia Encoder');app.setApplicationVersion(VERSION);app.setOrganizationName('nMedia');app.setStyle('Fusion');app.setStyleSheet(STYLES)
    palette=app.palette()
    for role,color in ((QPalette.ColorRole.Window,'#202020'),(QPalette.ColorRole.Base,'#181818'),(QPalette.ColorRole.Button,'#343332'),
        (QPalette.ColorRole.Text,'#eae7e3'),(QPalette.ColorRole.WindowText,'#eae7e3'),(QPalette.ColorRole.ButtonText,'#eae7e3'),
        (QPalette.ColorRole.Highlight,'#9a6743'),(QPalette.ColorRole.HighlightedText,'#ffffff')):palette.setColor(role,QColor(color))
    app.setPalette(palette)

def main(argv=None):
    app=QApplication.instance() or QApplication(sys.argv if argv is None else argv)
    apply_style(app)
    window=EncoderWindow();window.show()
    args=sys.argv[1:] if argv is None else argv[1:]
    if args and Path(args[0]).is_file():QTimer.singleShot(0,lambda:window.load_source(args[0]))
    return app.exec()
