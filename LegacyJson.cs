using System;
using System.IO;
using System.Text;
namespace MyDict
{
 // Reads one top-level map item at a time. Legacy migration never materializes the entire wordbook.
 internal sealed class LegacyJson:IDisposable
 {
  readonly TextReader reader;
  public LegacyJson(string path){reader=new StreamReader(path,Encoding.UTF8,true);}
  int Peek(){while(reader.Peek()>=0&&char.IsWhiteSpace((char)reader.Peek()))reader.Read();return reader.Peek();}
  void Expect(char c){if(Peek()!=c)throw new InvalidDataException("旧缓存 JSON 结构无效。");reader.Read();}
  string Raw()
  {
   int first=Peek();if(first<0)throw new EndOfStreamException();var text=new StringBuilder();bool quoted=false,escape=false;bool structured=first=='{'||first=='['||first=='"';int depth=0;
   while(reader.Peek()>=0){char c=(char)reader.Peek();if(text.Length>0&&!quoted&&depth==0&&(c==','||c=='}'||c==']'||char.IsWhiteSpace(c)))break;
    reader.Read();text.Append(c);if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;}
    else if(c=='"')quoted=true;else if(c=='{'||c=='[')depth++;else if(c=='}'||c==']')depth--;
    if(text.Length>16*1024*1024)throw new InvalidDataException("单条旧缓存过大。");
    if(structured&&!quoted&&depth==0)break;
   }
   if(quoted||depth!=0)throw new InvalidDataException("旧缓存 JSON 未完整写入。");return text.ToString();
  }
  string Name(){return JsonFiles.Serializer().Deserialize<string>(Raw());}
  void Map(Action<string,string> item){Expect('{');if(Peek()=='}'){reader.Read();return;}while(true){string key=Name();Expect(':');item(key,Raw());if(Peek()=='}'){reader.Read();return;}Expect(',');}}
  public void Read(string map,Action<string,string> item,Action<string,string> property,bool end=true)
  {
   if(Peek()=='['){Expect('[');int i=0;if(Peek()!=']')while(true){item((i++).ToString(),Raw());if(Peek()==']')break;Expect(',');}Expect(']');return;}
   Expect('{');if(Peek()=='}'){reader.Read();return;}while(true){string key=Name();Expect(':');if(key==map)Map(item);else property(key,Raw());if(Peek()=='}'){reader.Read();break;}Expect(',');}
   if(end&&Peek()!=-1)throw new InvalidDataException("旧缓存 JSON 尾部无效。");
  }
  public void ReadSync(Action<bool,string,string> item,Action<string,string> property)
  {
   Expect('{');while(Peek()!='}'){
    string name=Name();Expect(':');
    if(name=="Lookup"||name=="Favorites"){bool lookup=name=="Lookup";Read(lookup?"Results":"Changes",(key,raw)=>item(lookup,key,raw),property,false);}
    else property(name,Raw());
    if(Peek()=='}')break;Expect(',');
   }Expect('}');if(Peek()!=-1)throw new InvalidDataException("同步文件尾部无效。");
  }
  public void Dispose(){reader.Dispose();}
 }
}
