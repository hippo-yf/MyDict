#include "services.h"
#include <QtTest>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTimer>
#include <cstring>
#include <stdexcept>

using namespace MyDict;

#ifndef Q_MOC_RUN
namespace {
QJsonObject options() {
    return {{"Provider","MyMemory"},{"DeepSeek",QJsonObject{{"ApiKey","test-not-a-real-key"},
        {"Endpoint","https://api.deepseek.com/chat/completions"},{"Model","test-model"},
        {"TimeoutSeconds",10},{"MaxTokens",8192}}}};
}
QByteArray fixture() {
    return R"({"word":"go","summary":"\u53bb\uff1b\u524d\u5f80","translation":"\u53bb\uff1b\u524d\u5f80","phonetic":"/\u0261o\u028a/",
        "forms":{"ThirdPerson":"goes","PresentParticiple":"going","Past":"went","PastParticiple":"gone"},
        "derived":["goer (noun) \u5e38\u53bb\u67d0\u5904\u7684\u4eba"],"senses":[{"partOfSpeech":"verb",
        "definition":"To move.","chinese":"\u53bb\uff1b\u79fb\u52a8\u3002","collocations":[{"text":"go home","chinese":"\u56de\u5bb6"}],
        "examples":[{"text":"She went home.","chinese":"\u5979\u56de\u5bb6\u4e86\u3002","segments":[
        {"text":"She","role":"subject","pos":"pronoun"},{"text":" went","role":"predicate","pos":"verb"},
        {"text":" home.","role":"adverbial","pos":"adverb"}]}]}]})";
}
QByteArray response(QString content,QString reason="stop") {
    return QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{{"finish_reason",reason},
        {"message",QJsonObject{{"content",content}}}}}}}).toJson(QJsonDocument::Compact);
}
class Reply:public QNetworkReply {
    QByteArray bytes_;qint64 offset_=0;
public:
    Reply(QNetworkRequest request,QByteArray bytes,int status,int delay,QObject *parent):QNetworkReply(parent),bytes_(bytes) {
        setRequest(request);setUrl(request.url());setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,status);open(QIODevice::ReadOnly);
        QTimer::singleShot(delay,this,[this]{if(isFinished())return;setFinished(true);emit readyRead();emit finished();});
    }
    void abort() override {if(isFinished())return;setError(OperationCanceledError,"canceled");setFinished(true);emit finished();}
    qint64 bytesAvailable()const override{return bytes_.size()-offset_+QIODevice::bytesAvailable();}
protected:
    qint64 readData(char *data,qint64 max)override{
        const auto count=qMin(max,qint64(bytes_.size())-offset_);if(count<=0)return -1;
        std::memcpy(data,bytes_.constData()+offset_,size_t(count));offset_+=count;return count;
    }
};
class Manager:public QNetworkAccessManager {
public:
    struct Planned {QByteArray body;int status=200,delay=0;};
    QList<Planned> plans;QList<QByteArray> sent;QList<QNetworkRequest> requests;
    int active=0,maximumActive=0;
protected:
    QNetworkReply*createRequest(Operation,const QNetworkRequest &request,QIODevice *data)override{
        sent.append(data?data->readAll():QByteArray());requests.append(request);
        const auto p=plans.isEmpty()?Planned{response(QString::fromUtf8(fixture())),200,0}:plans.takeFirst();
        auto reply=new Reply(request,p.body,p.status,p.delay,this);
        maximumActive=qMax(maximumActive,++active);
        connect(reply,&QNetworkReply::finished,this,[this]{--active;});return reply;
    }
};
}
#endif

class ServiceTests: public QObject {
    Q_OBJECT
private slots:
    void flatBilingualEntry();
    void markdownAndChinese();
    void invalidSegmentsDropFormatting();
    void legacyMeanings();
    void invalidJsonAndSemanticErrors();
    void freeDictionary();
    void oneJsonCorrectionOnly();
    void repeatedInvalidJsonStops();
    void semanticErrorDoesNotSpendAgain();
    void truncatedAndHttpErrorsDoNotRetry();
    void cancelAndNewRequestDiscardOld();
    void validationNeverSends();
    void ordinaryLookupAddsPreciseSenseAndExampleTranslations();
};

#ifndef Q_MOC_RUN
void ServiceTests::flatBilingualEntry(){
        const auto entry=OnlineService::parseDeepSeek(QString::fromUtf8(fixture()),"go",options());
        QCOMPARE(entry.value("Word").toString(),QString("go"));
        QCOMPARE(entry.value("Summary").toString(),QString::fromUtf8("去；前往"));
        QCOMPARE(entry.value("LookupSource").toString(),QString("DeepSeek"));
        QCOMPARE(entry.value("LookupModel").toString(),QString("test-model"));
        const auto record=entry.value("Record").toObject();
        QCOMPARE(record.value("Forms").toObject().value("Past").toString(),QString("went"));
        const auto sense=record.value("Meanings").toArray().at(0).toObject().value("Senses").toArray().at(0).toObject();
        QCOMPARE(sense.value("Collocations").toArray().at(0).toString(),QString::fromUtf8("go home（回家）"));
        QCOMPARE(sense.value("Examples").toArray().at(0).toObject().value("Segments").toArray().size(),3);
        QVERIFY(!entry.value("Details").toString().contains("Free Dictionary"));
    }
void ServiceTests::markdownAndChinese(){
        const auto content=QChar(0xfeff)+QString("```json\n")+QString::fromUtf8(fixture())+"\n```";
        const auto entry=OnlineService::parseDeepSeek(content,QString::fromUtf8("去"),options());
        QCOMPARE(entry.value("Pair").toString(),QString("zh-CN|en"));
    }
void ServiceTests::invalidSegmentsDropFormatting(){
        auto data=QJsonDocument::fromJson(fixture()).object();auto senses=data.value("senses").toArray();
        auto sense=senses.at(0).toObject();auto examples=sense.value("examples").toArray();auto example=examples.at(0).toObject();
        example["segments"]=QJsonArray{QJsonObject{{"text","different text"},{"role","subject"}}};
        examples[0]=example;sense["examples"]=examples;senses[0]=sense;data["senses"]=senses;
        const auto entry=OnlineService::parseDeepSeek(QString::fromUtf8(QJsonDocument(data).toJson()),"go",options());
        const auto ex=entry.value("Record").toObject().value("Meanings").toArray().at(0).toObject().value("Senses").toArray().at(0).toObject().value("Examples").toArray().at(0).toObject();
        QVERIFY(ex.value("Segments").toArray().isEmpty());QCOMPARE(ex.value("Text").toString(),QString("She went home."));
    }
void ServiceTests::legacyMeanings(){
        const auto entry=OnlineService::parseDeepSeek(R"({"translation":"\u5bb6","meanings":[{"partOfSpeech":"noun","definitions":[{"definition":"A place to live.","chinese":"\u5c45\u6240\u3002"}]}]})","home",options());
        QCOMPARE(entry.value("Record").toObject().value("Meanings").toArray().size(),1);
    }
void ServiceTests::invalidJsonAndSemanticErrors(){
        QVERIFY_EXCEPTION_THROWN(OnlineService::parseDeepSeek("{incomplete","go",options()),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(OnlineService::parseDeepSeek("null","go",options()),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(OnlineService::parseDeepSeek("{}","go",options()),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(OnlineService::parseDeepSeek(R"({"translation":"\u53bb","senses":[{"definition":"To move."}]})","go",options()),std::runtime_error);
    }
void ServiceTests::freeDictionary(){
        const auto record=OnlineService::parseFreeDictionary("home",R"([{"word":"home","phonetics":[{"text":"/ho\u028am/"}],"license":{"name":"CC BY-SA","url":"https://creativecommons.org/"},"sourceUrls":["https://en.wiktionary.org/wiki/home"],"meanings":[{"partOfSpeech":"noun","synonyms":["house","house"],"definitions":[{"definition":"A place to live.","example":"This is my home."}]}]}])");
        QCOMPARE(record.value("Phonetic").toString(),QString::fromUtf8("/hoʊm/"));
        const auto m=record.value("Meanings").toArray().at(0).toObject();
        QCOMPARE(m.value("Synonyms").toArray().size(),1);
        QCOMPARE(m.value("Senses").toArray().at(0).toObject().value("Examples").toArray().size(),1);
        QVERIFY(record.value("Sources").toArray().size()>=3);
    }
void ServiceTests::oneJsonCorrectionOnly(){
        Manager manager;manager.plans={{response("{"),200,0},{response(QString::fromUtf8(fixture())),200,0}};
        OnlineService service;service.setNetworkAccessManagerForTests(&manager);QSignalSpy ready(&service,&OnlineService::ready),failed(&service,&OnlineService::failed);
        service.lookup(7,"go",options(),true);QTRY_COMPARE(ready.count(),1);QCOMPARE(failed.count(),0);QCOMPARE(manager.sent.size(),2);
        QCOMPARE(ready.at(0).at(0).toULongLong(),qulonglong(7));
        const auto sent=QJsonDocument::fromJson(manager.sent.at(1)).object();
        QCOMPARE(sent.value("messages").toArray().size(),4);QCOMPARE(sent.value("response_format").toObject().value("type").toString(),QString("json_object"));
        QCOMPARE(manager.requests.at(0).rawHeader("Authorization"),QByteArray("Bearer test-not-a-real-key"));
    }
void ServiceTests::repeatedInvalidJsonStops(){
        Manager manager;manager.plans={{response("{"),200,0},{response("{"),200,0}};
        OnlineService service;service.setNetworkAccessManagerForTests(&manager);QSignalSpy ready(&service,&OnlineService::ready),failed(&service,&OnlineService::failed);
        service.lookup(8,"go",options(),true);QTRY_COMPARE(failed.count(),1);QCOMPARE(ready.count(),0);QCOMPARE(manager.sent.size(),2);
    }
void ServiceTests::semanticErrorDoesNotSpendAgain(){
        Manager manager;manager.plans={{response(R"({"translation":"\u53bb","senses":[{"definition":"To move."}]})"),200,0}};
        OnlineService service;service.setNetworkAccessManagerForTests(&manager);QSignalSpy failed(&service,&OnlineService::failed);
        service.lookup(9,"go",options(),true);QTRY_COMPARE(failed.count(),1);QCOMPARE(manager.sent.size(),1);
    }
void ServiceTests::truncatedAndHttpErrorsDoNotRetry(){
        for(const auto &plan:QList<Manager::Planned>{{response("{}","length"),200,0},{"secret response body test-not-a-real-key",401,0}}){
            Manager manager;manager.plans={plan};OnlineService service;service.setNetworkAccessManagerForTests(&manager);QSignalSpy failed(&service,&OnlineService::failed);
            service.lookup(10,"go",options(),true);QTRY_COMPARE(failed.count(),1);QCOMPARE(manager.sent.size(),1);
            QVERIFY(!failed.at(0).at(1).toString().contains("test-not-a-real-key"));
        }
    }
void ServiceTests::cancelAndNewRequestDiscardOld(){
        Manager manager;manager.plans={{response(QString::fromUtf8(fixture())),200,100},{response(QString::fromUtf8(fixture())),200,0}};
        OnlineService service;service.setNetworkAccessManagerForTests(&manager);QSignalSpy ready(&service,&OnlineService::ready),failed(&service,&OnlineService::failed);
        service.lookup(11,"old",options(),true);service.lookup(12,"new",options(),true);QTRY_COMPARE(ready.count(),1);QTest::qWait(130);
        QCOMPARE(ready.count(),1);QCOMPARE(ready.at(0).at(0).toULongLong(),qulonglong(12));QCOMPARE(failed.count(),0);
    }
void ServiceTests::validationNeverSends(){
        Manager manager;OnlineService service;service.setNetworkAccessManagerForTests(&manager);QSignalSpy failed(&service,&OnlineService::failed);
        auto o=options();auto ds=o.value("DeepSeek").toObject();ds["Endpoint"]="http://api.deepseek.com/chat/completions";o["DeepSeek"]=ds;
        service.lookup(1,"go",o,true);QCOMPARE(failed.count(),1);QVERIFY(manager.sent.isEmpty());
        service.lookup(2,QString(501,'a'),options(),true);QCOMPARE(failed.count(),2);QVERIFY(manager.sent.isEmpty());
    }
void ServiceTests::ordinaryLookupAddsPreciseSenseAndExampleTranslations(){
        auto translated=[](QString s){return QJsonDocument(QJsonObject{{"responseStatus",200},
            {"responseData",QJsonObject{{"translatedText",s}}}}).toJson();};
        const QByteArray dictionary=R"([{"word":"home","phonetic":"/ho\u028am/","meanings":[{"partOfSpeech":"noun","definitions":[{"definition":"A place to live.","example":"This is my home."}]}]}])";
        const QByteArray wiki=R"({"parse":{"wikitext":{"*":"==English==\n===Derived terms===\n{{col|en|homeless}}"},"text":{"*":"<h2 id=\"English\">English</h2><p><span class=\"headword-line\"><b class=\"p-form-of\">homes</b></span></p>"}}})";
        Manager manager;manager.plans={{translated(QString::fromUtf8("家；住宅")),200,0},{dictionary,200,0},{wiki,200,0},
            {translated(QString::fromUtf8("供人居住的地方。")),200,10},{translated(QString::fromUtf8("这是我的家。")),200,10}};
        OnlineService service;service.setNetworkAccessManagerForTests(&manager);QSignalSpy ready(&service,&OnlineService::ready),failed(&service,&OnlineService::failed);
        service.lookup(20,"home",options(),false);QTRY_COMPARE(ready.count(),1);QCOMPARE(failed.count(),0);
        const auto e=ready.at(0).at(1).toJsonObject();const auto record=e.value("Record").toObject();
        const auto sense=record.value("Meanings").toArray().at(0).toObject().value("Senses").toArray().at(0).toObject();
        QCOMPARE(sense.value("Chinese").toString(),QString::fromUtf8("供人居住的地方。"));
        QCOMPARE(sense.value("Examples").toArray().at(0).toObject().value("Chinese").toString(),QString::fromUtf8("这是我的家。"));
        QCOMPARE(record.value("Forms").toObject().value("Plural").toString(),QString("homes"));
        QCOMPARE(record.value("Derived").toString(),QString("homeless"));
        QVERIFY(record.value("Sources").toArray().size()>=2);QCOMPARE(manager.sent.size(),5);QVERIFY(manager.maximumActive<=2);
    }
#endif

QTEST_MAIN(ServiceTests)
#include "services_test.moc"
