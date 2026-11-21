#include "dailypage.h"
#include "dailyword.h"
#include "entryrenderer.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QScrollBar>
#include <QDesktopServices>
#include <QSignalBlocker>
#include <QTimer>
namespace MyDict {
DailyPage::DailyPage(QWidget *parent):QWidget(parent){
    setObjectName("dailyPage");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(12,8,12,8);layout->setSpacing(6);
    auto *bar=new QHBoxLayout;todayButton=new QPushButton("今日");todayButton->setObjectName("dailyToday");bar->addWidget(todayButton);
    history=new QComboBox;history->setObjectName("dailyHistory");history->setMinimumWidth(100);history->setMaximumWidth(240);history->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);bar->addWidget(history,1);
    newer=new QPushButton("‹");older=new QPushButton("›");newer->setFixedWidth(24);older->setFixedWidth(24);newer->setAccessibleName("较新的往期词汇");older->setAccessibleName("更早的往期词汇");bar->addWidget(newer);bar->addWidget(older);
    bar->addStretch();retry=new QPushButton("生成 / 重试");retry->setObjectName("dailyRetry");bar->addWidget(retry);layout->addLayout(bar);
    status=new QLabel;status->setObjectName("dailyStatus");status->setWordWrap(true);status->hide();layout->addWidget(status);
    browser=new EntryBrowser;browser->setObjectName("dailyBrowser");browser->setOpenLinks(false);browser->setOpenExternalLinks(false);layout->addWidget(browser,1);
    connect(todayButton,&QPushButton::clicked,this,[this]{load({},true);});connect(retry,&QPushButton::clicked,this,[this]{load({},true);});
    connect(history,QOverload<int>::of(&QComboBox::activated),this,[this](int index){load(history->itemData(index).toString());});
    connect(older,&QPushButton::clicked,this,[this]{historyOffset+=20;loadHistory();});connect(newer,&QPushButton::clicked,this,[this]{historyOffset=qMax(0,historyOffset-20);loadHistory();});
    connect(browser,&QTextBrowser::anchorClicked,this,[this](QUrl url){if(url.scheme()=="word")emit lookupRequested(QUrl::fromPercentEncoding(url.toString(QUrl::FullyEncoded).mid(5).toLatin1()));else if(url.scheme()=="https")QDesktopServices::openUrl(url);});

}
DailyPage::~DailyPage(){cancel();}
void DailyPage::cancel(){if(canceled)canceled->store(true);++revision;busy=false;}
void DailyPage::configure(Database db,QJsonObject config){
    const bool identity=!database||database->path()!=db.path();
    if(identity){cancel();database=std::make_shared<Database>(db);article={};cacheLoaded=false;autoDate.clear();historyOffset=0;history->clear();status->clear();status->hide();browser->clear();setBusy(false);}
    const bool stylesChanged=settings!=config;settings=config;if(stylesChanged&&!article.isEmpty())render();
}
void DailyPage::setBusy(bool value){busy=value;retry->setEnabled(!value);todayButton->setEnabled(!value);history->setEnabled(!value);older->setEnabled(!value&&history->count()==20);newer->setEnabled(!value&&historyOffset>0);}
void DailyPage::run(std::function<QJsonObject()> job,std::function<void(QJsonObject)> done){
    if(busy)return;setBusy(true);const auto id=++revision;emit workStarted();
    struct Result{QJsonObject object;QString error;};auto *watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,id,done]{auto result=watcher->result();watcher->deleteLater();emit workFinished();if(id!=revision)return;setBusy(false);if(!result.error.isEmpty()){status->setText(result.error);status->show();return;}done(result.object);});
    watcher->setFuture(QtConcurrent::run([job]{try{return Result{job(),{}};}catch(const std::exception &e){return Result{{},QString::fromUtf8(e.what())};}}));
}
void DailyPage::activate(){
    if(!database||busy)return;
    try{const auto day=DailyWord::today(settings);const bool generate=autoDate!=day;if(!generate&&!article.isEmpty())return;autoDate=day;load(!generate&&!article.isEmpty()?article.value("Date").toString():QString(),generate);}catch(const std::exception &e){status->setText(QString::fromUtf8(e.what()));status->show();}
}
void DailyPage::load(QString date,bool generate){
    if(!database||busy)return;auto db=*database;auto config=settings;canceled=std::make_shared<std::atomic_bool>(false);auto token=canceled;
    const bool cacheFirst=generate&&!cacheLoaded;
    status->show();status->setText(generate?"正在读取缓存并准备今日词汇…":"正在读取专题…");
    run([db,config,date,generate,token,cacheFirst]()mutable{
        const auto day=date.isEmpty()?DailyWord::today(config):date;auto content=db.dailyArticle(day);QString error;bool generated=false;
        if(content.isEmpty()&&generate&&!cacheFirst){try{content=DailyWord::generate(db,config,token);generated=true;}catch(const std::exception &e){error=QString::fromUtf8(e.what());}}
        if(content.isEmpty()&&date.isEmpty())content=db.dailyArticle();
        if(!content.isEmpty()&&!cacheFirst&&content.value("SummaryVersion").toInt()<2){
            try{auto enriched=DailyWord::enrichSummary(db,content,config,token);generated=generated||enriched!=content;content=enriched;}catch(const std::exception &e){error=QString::fromUtf8(e.what());}
        }
        if(!content.isEmpty()&&!cacheFirst&&!token->load()){
            try{auto illustrated=DailyWord::enrichImage(db,content,config,token);generated=generated||illustrated!=content;content=illustrated;}catch(const std::exception &e){error=QString::fromUtf8(e.what());}
        }
        bool autoSaved=false;
        if(!content.isEmpty()&&!token->load()){
            auto entry=content.value("Entry").toObject();
            if(!db.isFavorite(content.value("Word").toString(),"en|zh-CN")){db.saveFavorite(entry);autoSaved=true;}
        }
        return QJsonObject{{"Day",day},{"Article",content},{"AutoSaved",autoSaved},{"Generated",generated},{"Error",error},{"History",db.dailyHistory(0)}};
    },[this,cacheFirst](QJsonObject result){
        cacheLoaded=true;
        const auto next=result.value("Article").toObject();bool same=!article.isEmpty()&&next.value("Date")==article.value("Date")&&next.value("Word")==article.value("Word");article=next;
        int scroll=same?browser->verticalScrollBar()->value():0;render();browser->verticalScrollBar()->setValue(scroll);
        const auto error=result.value("Error").toString();status->setText(!error.isEmpty()?error:article.isEmpty()?"尚无专题，点击生成 / 重试。":"");
        status->setVisible(!status->text().isEmpty());
        {QSignalBlocker block(history);historyOffset=0;history->clear();for(auto value:result.value("History").toArray()){auto row=value.toObject();history->addItem(row.value("Date").toString()+" · "+row.value("Word").toString(),row.value("Date"));}int index=history->findData(article.value("Date"));if(index>=0)history->setCurrentIndex(index);}
        setBusy(false);if(result.value("Generated").toBool()||result.value("AutoSaved").toBool())emit changed();
        if(cacheFirst&&(article.value("Date").toString()!=result.value("Day").toString()||article.value("SummaryVersion").toInt()<2||article.value("ImageCheckedOn").toString()!=result.value("Day").toString()))QTimer::singleShot(0,this,[this]{load({},true);});
    });
}
void DailyPage::loadHistory(){if(!database||busy)return;auto db=*database;const int offset=historyOffset;run([db,offset]{return QJsonObject{{"History",db.dailyHistory(offset)}};},[this](QJsonObject result){QSignalBlocker block(history);history->clear();for(auto value:result.value("History").toArray()){auto row=value.toObject();history->addItem(row.value("Date").toString()+" · "+row.value("Word").toString(),row.value("Date"));}setBusy(false);});}
void DailyPage::render(){
    if(article.isEmpty()){browser->setHtml(EntryRenderer::message("今日词汇","每天从新闻、科学或社会报道中，学习一个值得记住的词。",settings));return;}
    int position=browser->verticalScrollBar()->value();browser->setHtml(EntryRenderer::daily(article,settings));browser->verticalScrollBar()->setValue(position);
}
}
