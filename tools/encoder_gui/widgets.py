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
        self.title=ElidedLabel('Drop a video here');self.title.setObjectName('sourceTitle');self.title.setTextFormat(Qt.TextFormat.PlainText)
        self.detail=QLabel('Or choose a file from your computer.');self.detail.setObjectName('muted');self.detail.setTextFormat(Qt.TextFormat.PlainText)
        self.detail.setWordWrap(True)
        text=QVBoxLayout();text.setSpacing(6);text.addWidget(self.title);text.addWidget(self.detail)
        self.browse=QPushButton('Choose video…');self.browse.setIcon(icon('folder'));self.browse.setMinimumWidth(136)
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
        super().__init__();self.image=QPixmap();self.fit=(320,240);self.source_size=(0,0);self.crop='';self.aspect='';self.setMinimumHeight(160)
        self.setAccessibleName('Calculator framing preview')
    def set_image(self,image):self.image=image;self.update()
    def paintEvent(self,event):
        painter=QPainter(self);painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        frame=QRectF(self.rect()).adjusted(1,1,-1,-1)
        width=min(frame.width()-22,(frame.height()-22)*4/3);height=width*3/4
        screen=QRectF((frame.width()-width)/2,(frame.height()-height)/2,width,height)
        painter.setPen(QPen(QColor('#45423f'),1));painter.setBrush(QColor('#0b0b0b'));painter.drawRoundedRect(screen.adjusted(-5,-5,5,5),5,5)
        if not self.image.isNull():
            image=self.image
            if self.crop and self.source_size[0] and self.source_size[1]:
                try:
                    w,h,x,y=map(int,self.crop.split(':'));sx=image.width()/self.source_size[0];sy=image.height()/self.source_size[1]
                    if w>0 and h>0 and x+w<=self.source_size[0] and y+h<=self.source_size[1]:image=image.copy(int(x*sx),int(y*sy),max(1,int(w*sx)),max(1,int(h*sy)))
                except ValueError:pass
            elif self.aspect:
                parts=self.aspect.split(':');ratio=float(parts[0])/(float(parts[1]) if len(parts)>1 else 1)
                w=min(image.width(),int(image.height()*ratio));h=min(image.height(),int(image.width()/ratio))
                image=image.copy((image.width()-w)//2,(image.height()-h)//2,w,h)
            target=image.size().scaled(int(screen.width()*self.fit[0]/320),int(screen.height()*self.fit[1]/240),Qt.AspectRatioMode.KeepAspectRatio)
            box=QRectF(screen.center().x()-target.width()/2,screen.center().y()-target.height()/2,target.width(),target.height())
            painter.drawPixmap(box.toRect(),image)
        else:
            painter.setPen(QColor('#77736e'));painter.drawText(screen,Qt.AlignmentFlag.AlignCenter,'Your video, on a 320 × 240 screen')

STYLES="""
QWidget { background: transparent; color: #eae7e3; font-family: 'Public Sans', 'Noto Sans'; font-size: 13px; }
QMainWindow { background: #202020; }
QDialog { background: #262625; }
QLabel { background: transparent; }
QLabel#appTitle { font-size: 20px; font-weight: 600; }
QLabel#sourceTitle { font-size: 16px; font-weight: 600; }
QLabel#muted { color: #a6a19a; }
QLabel#sectionTitle { color: #eae7e3; font-weight: 600; font-size: 14px; }
QLabel#error { color: #ffaf9e; background: #3b2925; border: 1px solid #815347; border-radius: 5px; padding: 9px 12px; }
QFrame#sourceCard { background: #262625; border: 1px solid #45423e; border-radius: 7px; }
QFrame#sourceCard[dragging="true"] { border: 2px solid #e6a46c; }
QFrame#previewPanel { background: #191919; border: 1px solid #373533; border-radius: 6px; }
QFrame#footer { background: #262625; border-top: 1px solid #403d39; }
QFrame#divider { background: #3d3934; border: none; max-height: 1px; }
QPushButton { background: #343332; border: 1px solid #55504b; border-radius: 5px; padding: 8px 14px; min-height: 18px; }
QPushButton:hover { background: #42403d; border-color: #9b8672; }
QPushButton:pressed { background: #282725; border-color: #e6a46c; padding-top: 9px; padding-bottom: 7px; }
QPushButton:focus { border: 1px solid #e6a46c; }
QPushButton:disabled { background: #2b2b2a; color: #77736e; border-color: #3c3a37; }
QPushButton#primary { color: #25180e; background: #e6a46c; border: 1px solid #e6a46c; font-weight: 600; padding: 9px 24px; }
QPushButton#primary:hover { background: #f0b782; border-color: #f0b782; }
QPushButton#primary:pressed { background: #cc8b54; }
QPushButton#primary:disabled { background: #66503e; border-color: #66503e; color: #b6a18f; }
QToolButton { border: none; padding: 6px 0; color: #c7c1b9; background: transparent; }
QToolButton:hover { color: #f0b782; }
QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox { background: #181818; border: 1px solid #514b45; border-radius: 4px; padding: 7px 9px; min-height: 18px; selection-background-color: #755137; }
QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus { border-color: #e6a46c; }
QLineEdit[invalid="true"],QComboBox[invalid="true"],QSpinBox[invalid="true"],QDoubleSpinBox[invalid="true"] { border-color: #e8a18b; background: #332521; }
QLineEdit:disabled,QComboBox:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled { color: #78736d; border-color: #373431; background: #252422; }
QComboBox { padding-right: 28px; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url('@ASSETS@/down.svg'); width: 14px; height: 14px; }
QSpinBox,QDoubleSpinBox { padding-right: 22px; }
QSpinBox::up-button,QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 19px; background: #302d29; border-top-right-radius: 4px; }
QSpinBox::down-button,QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 19px; background: #302d29; border-bottom-right-radius: 4px; }
QSpinBox::up-button:hover,QSpinBox::down-button:hover,QDoubleSpinBox::up-button:hover,QDoubleSpinBox::down-button:hover { background: #544334; }
QSpinBox::up-arrow,QDoubleSpinBox::up-arrow { image: url('@ASSETS@/up.svg'); width: 12px; height: 12px; }
QSpinBox::down-arrow,QDoubleSpinBox::down-arrow { image: url('@ASSETS@/down.svg'); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: #282725; color: #eae7e3; border: 1px solid #615548; selection-background-color: #55402f; padding: 3px; }
QTabWidget::pane { border: 1px solid #46413c; border-radius: 5px; background: #242322; }
QTabBar::tab { background: transparent; color: #a8a29a; padding: 11px 25px; border-bottom: 2px solid transparent; margin-right: 4px; }
QTabBar::tab:selected { color: #f0b782; border-bottom-color: #e6a46c; }
QTabBar::tab:hover { color: #f2e4d6; background: #2c2926; }
QScrollArea { border: none; background: transparent; }
QScrollArea > QWidget > QWidget { background: #242322; }
QScrollBar:vertical { background: #252422; width: 9px; margin: 2px; }
QScrollBar::handle:vertical { background: #5a5149; border-radius: 3px; min-height: 30px; }
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height: 0; }
QCheckBox,QRadioButton { spacing: 9px; background: transparent; min-height: 24px; }
QCheckBox::indicator,QRadioButton::indicator { width: 15px; height: 15px; border: 1px solid #847567; background: #181818; border-radius: 3px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked,QRadioButton::indicator:checked { background: #e6a46c; border: 3px solid #4a3829; }
QCheckBox::indicator:checked,QTreeWidget::indicator:checked { image: url('@ASSETS@/check.svg'); background: #e6a46c; border: 1px solid #e6a46c; border-radius: 3px; }
QCheckBox::indicator:hover,QRadioButton::indicator:hover { border-color: #e6a46c; }
QTreeWidget { background: #1c1c1b; border: 1px solid #49443e; border-radius: 4px; outline: none; }
QTreeWidget::item { min-height: 34px; padding: 4px 7px; }
QTreeWidget::item:selected { background: #4c3b2d; }
QTreeWidget::item:hover { background: #35302b; }
QHeaderView::section { background: #302d29; border: none; border-bottom: 1px solid #49443e; color: #bdb4a9; padding: 7px; }
QProgressBar { background: #161615; border: none; border-radius: 3px; min-height: 6px; max-height: 6px; }
QProgressBar::chunk { background: #e6a46c; border-radius: 3px; }
QPlainTextEdit { background: #151515; border: 1px solid #49443e; border-radius: 4px; font-family: 'Cascadia Mono',monospace; font-size: 11px; padding: 7px; }
QToolTip { background: #38322c; color: #f0e7dd; border: 1px solid #746451; padding: 6px; }
""".replace('@ASSETS@',(Path(__file__).resolve().parent/'assets').as_posix())
