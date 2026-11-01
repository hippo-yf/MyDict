using System;
using System.Net;
using System.Text.RegularExpressions;
using System.Linq;
namespace MyDict
{
    public class WordForms
    {
        public string Plural { get; set; }
        public string ThirdPerson { get; set; }
        public string PresentParticiple { get; set; }
        public string Past { get; set; }
        public string PastParticiple { get; set; }
        public bool Uncountable { get; set; }
        public string ForPartOfSpeech(string part)
        {
            if (part.StartsWith("n.") || part == "noun") return !string.IsNullOrWhiteSpace(Plural) ? "复数 " + Plural : Uncountable ? "不可数" : "";
            if (!(part.StartsWith("v.") || part == "verb")) return "";
            return string.Join("  ·  ", new[] { Label("三单",ThirdPerson), Label("现在分词",PresentParticiple), Label("过去式",Past), Label("过去分词",PastParticiple) }.Where(x => x.Length > 0));
        }
        private static string Label(string label, string value) { return string.IsNullOrWhiteSpace(value) ? "" : label + " " + value; }
        public static WordForms Parse(string html)
        {
            var result = new WordForms();
            var english = Regex.Match(html ?? "", @"(?is)<h2\b[^>]*\bid=""English""[^>]*>.*?(?=<h2\b|\z)");
            if (!english.Success) return result;
            foreach (Match line in Regex.Matches(english.Value, @"(?is)<p>\s*<span\b[^>]*class=""headword-line"".*?</p>"))
            {
                foreach (Match form in Regex.Matches(line.Value, @"(?is)<(?:b|strong)\b(?<attrs>[^>]*)>(?<body>.*?)</(?:b|strong)>"))
                {
                    var attribute = Regex.Match(form.Groups["attrs"].Value, @"\bclass=""([^""]*)""");
                    var classes = WebUtility.HtmlDecode(attribute.Groups[1].Value).Split(' ');
                    string value = WebUtility.HtmlDecode(Regex.Replace(form.Groups["body"].Value, "<[^>]*>", "")).Trim();
                    if (value.Length == 0 || value.Length > 100) continue;
                    if (classes.Contains("p-form-of") && result.Plural == null) result.Plural = value;
                    if (classes.Contains("s-verb-form-form-of") && result.ThirdPerson == null) result.ThirdPerson = value;
                    if (classes.Contains("ing-form-form-of") && result.PresentParticiple == null) result.PresentParticiple = value;
                    if ((classes.Contains("spast-form-of") || (classes.Contains("spast|past|part-form-of") || classes.Contains("ed-form-form-of"))) && result.Past == null) result.Past = value;
                    if ((classes.Contains("past|part-form-of") || (classes.Contains("spast|past|part-form-of") || classes.Contains("ed-form-form-of"))) && result.PastParticiple == null) result.PastParticiple = value;
                }
                if (Regex.IsMatch(line.Value, @"(?i)<i>uncountable</i>")) result.Uncountable = true;
            }
            return result;
        }
    }
    public static class CambridgeLookup
    {
        public static string Url(string term)
        {
            term = (term ?? "").Trim();
            return term.Length == 0 ? "https://dictionary.cambridge.org/" : "https://dictionary.cambridge.org/search/english-chinese-simplified/direct/?q=" + Uri.EscapeDataString(term);
        }
    }
}


