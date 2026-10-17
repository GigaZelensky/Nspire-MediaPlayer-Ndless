from pathlib import Path
from PySide6.QtCore import Qt,QRectF,QSize,Signal
from PySide6.QtGui import QColor,QPainter,QPen,QPixmap,QIcon,QPainterPath
from PySide6.QtWidgets import QWidget,QFrame,QHBoxLayout,QVBoxLayout,QLabel,QPushButton,QSizePolicy

def icon(name,color="#d6d4d1",size=20):
    image=QPixmap(size,size);image.fill(Qt.GlobalColor.transparent)
    painter=QPainter(image);painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.scale(size/24,size/24);painter.setPen(QPen(QColor(color),1.7,Qt.PenStyle.SolidLine,Qt.PenCapStyle.RoundCap,Qt.PenJoinStyle.RoundJoin))
    if name=='folder':
        path=QPainterPath();path.moveTo(3,7);path.lineTo(9,7);path.lineTo(11,9);path.lineTo(21,9);path.lineTo(21,20);path.lineTo(3,20);path.closeSubpath();painter.drawPath(path)
        painter.drawLine(3,7,3,5);painter.drawLine(3,5,10,5);painter.drawLine(10,5,12,7);painter.drawLine(12,7,20,7)
    elif name=='video':
        painter.drawRoundedRect(QRectF(3,4,18,16),2,2);painter.drawLine(8,4,8,20);painter.drawLine(16,4,16,20)
        for y in (8,12,16):painter.drawLine(3,y,8,y);painter.drawLine(16,y,21,y)
    elif name=='copy':
        painter.drawRoundedRect(QRectF(8,8,12,13),2,2);painter.drawLine(4,16,4,3);painter.drawLine(4,3,16,3)
    elif name=='play':
        path=QPainterPath();path.moveTo(8,5);path.lineTo(19,12);path.lineTo(8,19);path.closeSubpath();painter.drawPath(path)
    elif name=='close':painter.drawLine(6,6,18,18);painter.drawLine(18,6,6,18)
    elif name=='check':painter.drawLine(5,12,10,17);painter.drawLine(10,17,20,6)
    painter.end();return QIcon(image)

class ElidedLabel(QLabel):
    def __init__(self,text):
        super().__init__(text);self.setSizePolicy(QSizePolicy.Policy.Ignored,QSizePolicy.Policy.Fixed);self.setMinimumHeight(24)
    def paintEvent(self,event):
        painter=QPainter(self);painter.setFont(self.font());painter.setPen(self.palette().color(self.foregroundRole()))
        painter.drawText(self.contentsRect(),Qt.AlignmentFlag.AlignVCenter,self.fontMetrics().elidedText(self.text(),Qt.TextElideMode.ElideMiddle,self.width()))

class SourceCard(QFrame):
    selected=Signal(str)
    def __init__(self):
        super().__init__();self.setObjectName('sourceCard');self.setAcceptDrops(True)
        self.preview=QLabel();self.preview.setFixedSize(84,52);self.preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview.setPixmap(icon('video','#8c8985',32).pixmap(32,32))
        self.title=ElidedLabel('Drop a video or music file here');self.title.setObjectName('sourceTitle');self.title.setTextFormat(Qt.TextFormat.PlainText)
        self.detail=QLabel('Or choose a file from your computer.');self.detail.setObjectName('muted');self.detail.setTextFormat(Qt.TextFormat.PlainText)
        self.detail.setWordWrap(True)
        text=QVBoxLayout();text.setSpacing(6);text.addWidget(self.title);text.addWidget(self.detail)
        self.browse=QPushButton('Choose file…');self.browse.setIcon(icon('folder'));self.browse.setMinimumWidth(136)
        row=QHBoxLayout(self);row.setContentsMargins(16,12,16,12);row.setSpacing(18);row.addWidget(self.preview);row.addLayout(text,1);row.addWidget(self.browse)
    def dragEnterEvent(self,event):
        if event.mimeData().hasUrls() and len(event.mimeData().urls())==1 and event.mimeData().urls()[0].isLocalFile():
            self.setProperty('dragging',True);self.style().unpolish(self);self.style().polish(self);event.acceptProposedAction()
    def dragLeaveEvent(self,event):
        self.setProperty('dragging',False);self.style().unpolish(self);self.style().polish(self)
    def dropEvent(self,event):
        self.dragLeaveEvent(event);self.selected.emit(event.mimeData().urls()[0].toLocalFile());event.acceptProposedAction()
    def set_image(self,pixmap):self.preview.setPixmap(pixmap.scaled(self.preview.size(),Qt.AspectRatioMode.KeepAspectRatio,Qt.TransformationMode.SmoothTransformation))

class Preview(QWidget):
    def __init__(self):
        super().__init__();self.image=QPixmap();self.fit=(320,240);self.source_size=(0,0);self.crop='';self.aspect='';self.audio=False;self.setMinimumHeight(160)
        self.setAccessibleName('Calculator framing preview')
    def set_image(self,image):self.image=image;self.update()
    def paintEvent(self,event):
        painter=QPainter(self);painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        frame=QRectF(self.rect()).adjusted(1,1,-1,-1)
        ratio=1 if self.audio else 4/3
        width=min(frame.width()-22,(frame.height()-22)*ratio);height=width/ratio
        screen=QRectF((frame.width()-width)/2,(frame.height()-height)/2,width,height)
        painter.setPen(QPen(QColor('#45423f'),1));painter.setBrush(QColor('#0b0b0b'));painter.drawRoundedRect(screen.adjusted(-5,-5,5,5),5,5)
        if not self.image.isNull():
            image=self.image
            if not self.audio and self.crop and self.source_size[0] and self.source_size[1]:
                try:
                    w,h,x,y=map(int,self.crop.split(':'));sx=image.width()/self.source_size[0];sy=image.height()/self.source_size[1]
                    if w>0 and h>0 and x+w<=self.source_size[0] and y+h<=self.source_size[1]:image=image.copy(int(x*sx),int(y*sy),max(1,int(w*sx)),max(1,int(h*sy)))
                except ValueError:pass
            elif not self.audio and self.aspect:
                parts=self.aspect.split(':');ratio=float(parts[0])/(float(parts[1]) if len(parts)>1 else 1)
                w=min(image.width(),int(image.height()*ratio));h=min(image.height(),int(image.width()/ratio))
                image=image.copy((image.width()-w)//2,(image.height()-h)//2,w,h)
            target=image.size().scaled(int(screen.width()*(1 if self.audio else self.fit[0]/320)),int(screen.height()*(1 if self.audio else self.fit[1]/240)),Qt.AspectRatioMode.KeepAspectRatio)
            box=QRectF(screen.center().x()-target.width()/2,screen.center().y()-target.height()/2,target.width(),target.height())
            painter.drawPixmap(box.toRect(),image)
        else:
            painter.setPen(QColor('#77736e'));painter.drawText(screen,Qt.AlignmentFlag.AlignCenter,'No embedded album art' if self.audio else 'Your video, on a 320 × 240 screen')

STYLES="""
QWidget { background: transparent; color: #eae7e3; font-family: 'Public Sans', 'Noto Sans'; font-size: 13px; }
QMainWindow { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #292827,stop:0.22 #1c1b1a,stop:1 #111110); }
QDialog { background: #211e1b; }
QLabel { background: transparent; }
QLabel#appTitle { font-size: 20px; font-weight: 600; }
QLabel#sourceTitle { font-size: 16px; font-weight: 600; }
QLabel#muted { color: #b5aaa0; }
QLabel#sectionTitle { color: #eae7e3; font-weight: 600; font-size: 14px; }
QLabel#error { color: #ffaf9e; background: #3b2925; border: 1px solid #815347; border-radius: 5px; padding: 9px 12px; }
QFrame#appHeader { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #514940,stop:0.48 #2c2723,stop:0.50 #211d1a,stop:1 #171412); border: 1px solid #100d0b; border-top-color: #746456; border-bottom: 2px solid #9e642c; border-radius: 6px; }
QFrame#sourceCard { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #36322e,stop:0.5 #292623,stop:1 #22201e); border: 1px solid #15110e; border-top-color: #5b4e42; border-left-color: #4c4036; border-radius: 6px; }
QFrame#sourceCard[dragging="true"] { border: 2px solid #e6a46c; }
QFrame#previewPanel { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #242220,stop:1 #141312); border: 1px solid #4c4137; border-top-color: #080706; border-left-color: #0b0908; border-radius: 6px; }
QFrame#footer { background: #262625; border-top: 1px solid #403d39; }
QFrame#divider { background: #44382e; border: none; border-bottom: 1px solid #0b0908; max-height: 2px; }
QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #68605a,stop:0.47 #49413b,stop:0.50 #312b27,stop:1 #24201d); border: 1px solid #100c09; border-top-color: #8e7d6e; border-left-color: #655548; border-radius: 5px; padding: 8px 14px; min-height: 18px; }
QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #81705f,stop:0.47 #5d4a3c,stop:0.50 #413025,stop:1 #2b2119); border-color: #c08b53; border-top-color: #edbd8a; }
QPushButton:pressed { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #221a14,stop:0.5 #342519,stop:1 #514033); border-top-color: #17100b; border-left-color: #20140c; border-bottom-color: #ba8b5b; padding-top: 9px; padding-bottom: 7px; }
QPushButton:focus { border-color: #e6a46c; }
QPushButton:disabled { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #393430,stop:0.5 #2a2521,stop:1 #211d1a); color: #918478; border-color: #3f352c; border-top-color: #55483c; }
QPushButton#primary { color: #fff4e6; background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #b67a3b,stop:0.47 #965a26,stop:0.50 #723c13,stop:1 #4d260c); border: 1px solid #3e1e08; border-top-color: #f0bf87; border-left-color: #c58a4b; font-weight: 600; padding: 9px 24px; }
QPushButton#primary:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #d5944c,stop:0.47 #ab682b,stop:0.50 #874917,stop:1 #65300e); border-color: #edb773; }
QPushButton#primary:pressed { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #452109,stop:0.5 #70390f,stop:1 #9a5d29); border-top-color: #331705; border-bottom-color: #e0a56b; padding-top: 10px; padding-bottom: 8px; }
QPushButton#primary:disabled { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #66503a,stop:1 #352719); border-color: #513e2c; color: #b6a18f; }
QToolButton { border: none; padding: 6px 0; color: #c7c1b9; background: transparent; }
QToolButton:hover { color: #f0b782; }
QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #0d0c0b,stop:0.16 #151210,stop:1 #211c18); border: 1px solid #594a3d; border-top-color: #080605; border-left-color: #0c0907; border-radius: 4px; padding: 7px 9px; min-height: 18px; selection-background-color: #755137; }
QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus { border-color: #e6a46c; }
QLineEdit[invalid="true"],QComboBox[invalid="true"],QSpinBox[invalid="true"],QDoubleSpinBox[invalid="true"] { border-color: #e8a18b; background: #332521; }
QLineEdit:disabled,QComboBox:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled { color: #78736d; border-color: #373431; background: #252422; }
QComboBox { padding-right: 28px; background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #47423c,stop:0.48 #332e29,stop:0.51 #25211e,stop:1 #1c1916); border-top-color: #73604c; border-left-color: #594735; border-bottom-color: #0c0805; }
QComboBox:hover { border-color: #ad7a46; }
QComboBox::drop-down { border: none; border-left: 1px solid #655444; width: 26px; background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #686058,stop:0.48 #494038,stop:0.51 #332b24,stop:1 #231e19); border-top-right-radius: 3px; border-bottom-right-radius: 3px; }
QComboBox::drop-down:pressed { background: #342313; }
QComboBox::down-arrow { image: url('@ASSETS@/down.svg'); width: 14px; height: 14px; }
QSpinBox,QDoubleSpinBox { padding-right: 22px; }
QSpinBox::up-button,QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 19px; background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #65513e,stop:1 #34271c); border: 1px solid #20150d; border-top-color: #8e7155; border-top-right-radius: 4px; }
QSpinBox::down-button,QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 19px; background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #4a3828,stop:1 #291b11); border: 1px solid #1b110a; border-top-color: #6d523b; border-bottom-right-radius: 4px; }
QSpinBox::up-button:hover,QSpinBox::down-button:hover,QDoubleSpinBox::up-button:hover,QDoubleSpinBox::down-button:hover { background: #544334; }
QSpinBox::up-arrow,QDoubleSpinBox::up-arrow { image: url('@ASSETS@/up.svg'); width: 12px; height: 12px; }
QSpinBox::down-arrow,QDoubleSpinBox::down-arrow { image: url('@ASSETS@/down.svg'); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: #282725; color: #eae7e3; border: 1px solid #615548; selection-background-color: #55402f; padding: 3px; }
QTabWidget::pane { border: 1px solid #5a4736; border-top-color: #896d52; border-bottom-color: #090604; border-radius: 5px; background: #211b16; }
QTabBar::tab { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #51473d,stop:0.48 #302821,stop:1 #19130f); color: #c6b7a9; padding: 10px 25px; border: 1px solid #100b07; border-top-color: #7c6a59; border-top-left-radius: 5px; border-top-right-radius: 5px; margin-right: 3px; margin-top: 3px; }
QTabBar::tab:selected { color: #ffe0b4; background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #8f623a,stop:0.48 #51361f,stop:1 #2a1e14); border-top-color: #edb77b; border-bottom-color: #58402a; margin-top: 0; padding-top: 13px; }
QTabBar::tab:hover { color: #fff0dd; border-top-color: #d29a60; }
QTabBar::tab:disabled { color: #8b7b6d; background: #28221c; border-top-color: #473b31; }
QScrollArea { border: none; background: transparent; }
QScrollArea > QWidget > QWidget { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #292624,stop:1 #1e1c1a); }
QScrollBar:vertical { background: #100c09; border: 1px solid #413123; width: 11px; margin: 2px; }
QScrollBar::handle:vertical { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #8e8175,stop:0.45 #5a5047,stop:1 #302a24); border: 1px solid #8b7057; border-radius: 3px; min-height: 30px; }
QScrollBar::handle:vertical:hover { border-color: #e6a46c; }
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height: 0; }
QCheckBox,QRadioButton { spacing: 9px; background: transparent; min-height: 24px; }
QCheckBox::indicator,QRadioButton::indicator { width: 15px; height: 15px; border: 1px solid #7f6248; border-top-color: #0d0805; border-left-color: #160e08; background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #120c07,stop:1 #4c3724); border-radius: 3px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked,QRadioButton::indicator:checked { background: #e6a46c; border: 3px solid #4a3829; }
QCheckBox::indicator:checked,QTreeWidget::indicator:checked { image: url('@ASSETS@/check.svg'); background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffe0b2,stop:0.45 #eeb579,stop:0.5 #d08b49,stop:1 #b77232); border: 1px solid #9f662f; border-top-color: #ffdfb0; border-radius: 3px; }
QCheckBox::indicator:hover,QRadioButton::indicator:hover { border-color: #e6a46c; }
QTreeWidget { background: #17120e; border: 1px solid #5e4937; border-top-color: #090603; border-left-color: #0e0905; border-radius: 4px; outline: none; }
QTreeWidget::item { min-height: 34px; padding: 4px 7px; }
QTreeWidget::item:selected { background: #4c3b2d; }
QTreeWidget::item:hover { background: #35302b; }
QHeaderView::section { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #655342,stop:0.48 #3e2f23,stop:0.51 #2e2118,stop:1 #251a12); border: 1px solid #1a1009; border-top-color: #8f7258; color: #ead5c2; padding: 7px; }
QProgressBar { background: #100b07; border: 1px solid #503924; border-top-color: #040302; border-radius: 4px; min-height: 8px; max-height: 8px; }
QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffe2b8,stop:0.45 #e8ac66,stop:0.5 #b96e28,stop:1 #804112); border-radius: 3px; }
QPlainTextEdit { background: #151515; border: 1px solid #49443e; border-radius: 4px; font-family: 'Cascadia Mono',monospace; font-size: 11px; padding: 7px; }
QToolTip { background: #38322c; color: #f0e7dd; border: 1px solid #746451; padding: 6px; }
""".replace('@ASSETS@',(Path(__file__).resolve().parent/'assets').as_posix())
