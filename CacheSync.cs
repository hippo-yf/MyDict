using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace MyDict
{
    public class CacheSyncOptions
    {
        public bool Enabled { get; set; } = true;
        public string Repository { get; set; } = "git@gitee.com:hippo-yf/MyDict-Cache.git";
        public string PrivateKeyPath { get; set; } = "";
        public string KnownHostsPath { get; set; } = "";
        public string DeviceId { get; set; }
        public void Validate()
        {
            if (!Regex.IsMatch(Repository ?? "", @"^git@gitee\.com:[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+\.git$")) throw new InvalidOperationException("CacheSync.Repository 必须是 Gitee SSH 仓库地址。");
            if (!string.IsNullOrEmpty(DeviceId) && !Regex.IsMatch(DeviceId, "^[a-f0-9]{32}$")) throw new InvalidOperationException("CacheSync.DeviceId 必须是 32 位小写十六进制设备标识。");
            if ((PrivateKeyPath ?? "").IndexOfAny(new[] {'\r','\n','\"'}) >= 0) throw new InvalidOperationException("密钥路径包含无效字符。");
            if ((KnownHostsPath ?? "").IndexOfAny(new[] {'\r','\n','\"'}) >= 0) throw new InvalidOperationException("主机密钥文件路径包含无效字符。");
        }
    }

    public static class JsonFiles
    {
        public static JavaScriptSerializer Serializer() { return new JavaScriptSerializer { MaxJsonLength = 256 * 1024 * 1024, RecursionLimit = 128 }; }
        public static T Read<T>(string path) { return Serializer().Deserialize<T>(File.ReadAllText(path, Encoding.UTF8)); }
        public static void Write(string path, object value)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            File.WriteAllText(path + ".tmp", SettingsStore.FormatJson(Serializer().Serialize(value)), new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(path + ".tmp", path, path + ".bak"); else File.Move(path + ".tmp", path);
        }
    }

    public class FavoriteChange
    {
        public Entry Entry { get; set; }
        public bool Deleted { get; set; }
        public string UpdatedAtUtc { get; set; }
    }
    public class FavoriteData
    {
        public string username { get; set; }
        public Dictionary<string, FavoriteChange> Changes { get; set; } = new Dictionary<string, FavoriteChange>();
    }
    public static class CacheMerge
    {
        public static string FavoriteKey(Entry e) { return e.Word.Trim().Normalize().ToLowerInvariant() + "|" + e.Pair; }
        public static int Compare(Entry a, Entry b)
        {
            var order = QueryTime(a).CompareTo(QueryTime(b));
            return order != 0 ? order : string.CompareOrdinal(JsonFiles.Serializer().Serialize(a), JsonFiles.Serializer().Serialize(b));
        }
        public static DateTimeOffset QueryTime(Entry entry)
        {
            DateTimeOffset time;
            foreach (string value in new[] { entry.SuccessfulQueryAtUtc, entry.QueriedAtUtc, entry.SavedAt })
                if (DateTimeOffset.TryParse(value, System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.AssumeUniversal, out time)) return time.ToUniversalTime();
            return DateTimeOffset.MinValue;
        }
        public static void MergeFavorites(FavoriteData target, FavoriteData source)
        {
            if (source == null || source.username != target.username || source.Changes == null) return;
            foreach (var pair in source.Changes)
            {
                var change = pair.Value;
                if (change == null || change.Entry == null || string.IsNullOrWhiteSpace(change.Entry.Word) || string.IsNullOrEmpty(change.UpdatedAtUtc)) continue;
                FavoriteChange old;
                if (!target.Changes.TryGetValue(pair.Key, out old) || string.CompareOrdinal(change.UpdatedAtUtc, old.UpdatedAtUtc) > 0 || (change.UpdatedAtUtc == old.UpdatedAtUtc && change.Deleted)) target.Changes[pair.Key] = change;
            }
        }
    }

    public static class CacheIdentity
    {
        public static string UsernameForDevice(string deviceId, string accountId)
        {
            using (var hash = System.Security.Cryptography.SHA256.Create()) return "device-" + BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(deviceId + "\n" + accountId))).Replace("-", "").ToLowerInvariant().Substring(0,24);
        }
        public static string DefaultUsername()
        {
            string device = Environment.MachineName;
            try {
                using (var key = Microsoft.Win32.RegistryKey.OpenBaseKey(Microsoft.Win32.RegistryHive.LocalMachine, Microsoft.Win32.RegistryView.Registry64))
                using (var cryptography = key.OpenSubKey(@"SOFTWARE\Microsoft\Cryptography")) {
                    var guid = cryptography == null ? null : Convert.ToString(cryptography.GetValue("MachineGuid"));
                    if (!string.IsNullOrWhiteSpace(guid)) device = guid;
                }
            } catch (System.Security.SecurityException) { } catch (UnauthorizedAccessException) { } catch (IOException) { }
            string account = Environment.UserDomainName + "\\" + Environment.UserName;
            try { account = System.Security.Principal.WindowsIdentity.GetCurrent().User.Value; } catch (System.Security.SecurityException) { }
            return UsernameForDevice(device, account);
        }
        public static string DeviceId(string seed)
        {
            // Copied settings may share a seed. Keep their Git shards distinct on other devices/accounts.
            var identity = Environment.MachineName + "\n" + Environment.UserDomainName + "\\" + Environment.UserName + "\n" + seed;
            using (var hash = System.Security.Cryptography.SHA256.Create()) return BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(identity))).Replace("-", "").ToLowerInvariant().Substring(0,32);
        }
    }

    public class SyncDocument
    {
        public string username { get; set; }
        public string DeviceId { get; set; }
        public LookupData Lookup { get; set; }
        public FavoriteData Favorites { get; set; }
    }

    // Git operations run off the UI thread. Each device writes only its own shard;
    // other users' documents and concurrent devices are never overwritten.
    public class GiteeCacheSync
    {
        private readonly string directory;
        private readonly CacheSyncOptions options;
        public GiteeCacheSync(string directory, CacheSyncOptions options) { this.directory = directory; this.options = options; }
        private static string Quote(string text)
        {
            var result = new StringBuilder("\""); int slashes = 0;
            foreach (char c in text) {
                if (c == '\\') { slashes++; continue; }
                result.Append('\\', c == '"' ? slashes * 2 + 1 : slashes).Append(c); slashes = 0;
            }
            return result.Append('\\', slashes * 2).Append('"').ToString();
        }
        private static string ShellQuote(string text) { return "'" + text.Replace("'", "'\\''") + "'"; }
        public static string ResolveLocalPath(string path)
        {
            string expanded = Environment.ExpandEnvironmentVariables(path);
            return Path.GetFullPath(Path.IsPathRooted(expanded) ? expanded : Path.Combine(Path.GetDirectoryName(SettingsStore.FilePath), expanded)).Replace('\\', '/');
        }
        private string Git(string args, bool allowFailure = false)
        {
            var info = new ProcessStartInfo("git", args) { WorkingDirectory = directory, UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true, StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8 };
            info.EnvironmentVariables["GIT_TERMINAL_PROMPT"] = "0";
            // Default SSH config/agent is supported. Unknown host keys fail rather than prompting.
            string ssh = "ssh -o BatchMode=yes -o ConnectTimeout=15 -o StrictHostKeyChecking=yes";
            if (!string.IsNullOrWhiteSpace(options.PrivateKeyPath)) ssh += " -o IdentitiesOnly=yes -i " + ShellQuote(ResolveLocalPath(options.PrivateKeyPath));
            if (!string.IsNullOrWhiteSpace(options.KnownHostsPath)) ssh += " -o " + ShellQuote("UserKnownHostsFile=\"" + ResolveLocalPath(options.KnownHostsPath) + "\"");
            info.EnvironmentVariables["GIT_SSH_COMMAND"] = ssh;
            using (var process = Process.Start(info))
            {
                var output = process.StandardOutput.ReadToEndAsync(); var error = process.StandardError.ReadToEndAsync();
                if (!process.WaitForExit(45000)) { process.Kill(); throw new IOException("Gitee 同步超时，本地数据已保留。"); }
                Task.WaitAll(output, error);
                if (process.ExitCode != 0 && !allowFailure) throw new IOException("Gitee 同步失败：" + error.Result.Trim());
                return process.ExitCode == 0 ? output.Result.Trim() : null;
            }
        }
        private void RebaseRemote()
        {
            // Preserve tracked edits/deletions from an interrupted sync or a manual migration.
            // Git retains the autostash if it cannot be reapplied; never reset or clean user data.
            Git("rebase --autostash origin/main");
            if(!string.IsNullOrWhiteSpace(Git("diff --name-only --diff-filter=U")))
                throw new IOException("Gitee 工作目录存在合并冲突，本地修改已保留；请解决冲突后重试同步。");
        }
        public Task<int> RetireLegacyJsonAsync()
        {
            return Task.Run(()=>{
                options.Validate();
                using(var syncLock=new FileStream(Path.Combine(directory,"sync.lock"),FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None)){
                    if(Git("config --get remote.origin.url")!=options.Repository)throw new IOException("同步仓库不匹配。");
                    Git("config user.name MyDict");Git("config user.email mydict@users.noreply.gitee.com");Git("fetch origin");RebaseRemote();
                    var files=Git("ls-files -z -- users").Split(new[]{'\0'},StringSplitOptions.RemoveEmptyEntries).Where(path=>Regex.IsMatch(path,@"^users/[a-z0-9_-]{1,64}/[^/]+\.json$")).ToList();
                    if(files.Count==0)return 0;
                    foreach(string relative in files)Git("rm -f -- "+Quote(relative));
                    Git("-c commit.gpgsign=false commit -m "+Quote("Remove obsolete JSON cache snapshots")+" --only -- "+string.Join(" ",files.Select(Quote)));
                    for(int attempt=0;attempt<3;attempt++){if(Git("push origin HEAD:main",true)!=null)return files.Count;Git("fetch origin");RebaseRemote();}
                    throw new IOException("JSON 删除提交推送失败，本地提交已保留。");
                }
            });
        }
        public Task ExchangeDatabaseAsync(WordDatabase database,string deviceId,bool upload)
        {
            return Task.Run(()=>{
                options.Validate();Directory.CreateDirectory(directory);
                using(var syncLock=new FileStream(Path.Combine(directory,"sync.lock"),FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None))
                {
                    if(!Directory.Exists(Path.Combine(directory,".git"))){Git("init --initial-branch=main");Git("remote add origin "+Quote(options.Repository));}
                    if(Git("config --get remote.origin.url")!=options.Repository)throw new IOException("同步仓库地址已变更。");
                    Git("config user.name MyDict");Git("config user.email mydict@users.noreply.gitee.com");Git("fetch origin");
                    bool remote=Git("rev-parse --verify refs/remotes/origin/main",true)!=null,local=Git("rev-parse --verify HEAD",true)!=null;
                    if(remote){if(local)RebaseRemote();else Git("checkout -B main origin/main");}
                    string folder=Path.Combine(directory,"users",database.Username);Directory.CreateDirectory(folder);
                    foreach(string file in Directory.GetFiles(folder,"*.sqlite"))database.MergeFrom(file);
                    if(!upload)return;
                    string shard=Path.Combine(folder,deviceId+".sqlite");long revision=database.BackupTo(shard);
                    Git("add -- "+Quote("users/"+database.Username+"/"+deviceId+".sqlite"));
                    if(!string.IsNullOrEmpty(Git("diff --cached --name-only -- "+Quote("users/"+database.Username+"/"+deviceId+".sqlite"))))Git("-c commit.gpgsign=false commit -m "+Quote("Sync SQLite cache for "+database.Username)+" --only -- "+Quote("users/"+database.Username+"/"+deviceId+".sqlite"));
                    for(int attempt=0;attempt<3;attempt++){
                        if(Git("push origin HEAD:main",true)!=null){database.MarkSynced(revision);return;}
                        Git("fetch origin");RebaseRemote();
                    }
                    throw new IOException("Gitee 推送失败；SQLite 本地数据已保存，下次同步将重试。");
                }
            });
        }

    }

    public sealed class DeferredSave
    {
        public DateTime? DueUtc { get; private set; }
        public bool Dirty { get { return DueUtc.HasValue; } }
        public void Mark(DateTime now) { if (!Dirty) DueUtc = now.AddMinutes(30); }
        public void Clear() { DueUtc = null; }
        public bool IsDue(DateTime now) { return DueUtc.HasValue && now >= DueUtc.Value; }
    }
}
