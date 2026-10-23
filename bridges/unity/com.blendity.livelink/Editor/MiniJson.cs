// SPDX-License-Identifier: GPL-2.0-or-later
// A small JSON reader (Unity's JsonUtility reads only into fixed classes): objects as Dictionary<string, object>,
// arrays as List<object>, numbers as double, strings, bools, null. And typed getters for Live Link's messages.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Blendity.LiveLink
{
    public static class MiniJson
    {
        public static object Parse(string text)
        {
            int i = 0;
            object v = Value(text, ref i);
            Space(text, ref i);
            if (i != text.Length)
                throw new FormatException("text after the JSON value");
            return v;
        }

        static void Space(string s, ref int i)
        {
            while (i < s.Length && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
                i++;
        }

        static object Value(string s, ref int i)
        {
            Space(s, ref i);
            if (i >= s.Length)
                throw new FormatException("JSON ends early");
            char c = s[i];
            if (c == '{')
                return Obj(s, ref i);
            if (c == '[')
                return Arr(s, ref i);
            if (c == '"')
                return Str(s, ref i);
            if (Word(s, ref i, "true"))
                return true;
            if (Word(s, ref i, "false"))
                return false;
            if (Word(s, ref i, "null"))
                return null;
            return Num(s, ref i);
        }

        static bool Word(string s, ref int i, string w)
        {
            if (string.CompareOrdinal(s, i, w, 0, w.Length) != 0)
                return false;
            i += w.Length;
            return true;
        }

        static Dictionary<string, object> Obj(string s, ref int i)
        {
            var d = new Dictionary<string, object>();
            i++;
            Space(s, ref i);
            if (i < s.Length && s[i] == '}')
            {
                i++;
                return d;
            }
            for (;;)
            {
                Space(s, ref i);
                string k = Str(s, ref i);
                Space(s, ref i);
                Expect(s, ref i, ':');
                d[k] = Value(s, ref i);
                Space(s, ref i);
                if (i < s.Length && s[i] == ',')
                {
                    i++;
                    continue;
                }
                Expect(s, ref i, '}');
                return d;
            }
        }

        static List<object> Arr(string s, ref int i)
        {
            var a = new List<object>();
            i++;
            Space(s, ref i);
            if (i < s.Length && s[i] == ']')
            {
                i++;
                return a;
            }
            for (;;)
            {
                a.Add(Value(s, ref i));
                Space(s, ref i);
                if (i < s.Length && s[i] == ',')
                {
                    i++;
                    continue;
                }
                Expect(s, ref i, ']');
                return a;
            }
        }

        static void Expect(string s, ref int i, char c)
        {
            if (i >= s.Length || s[i] != c)
                throw new FormatException("expected '" + c + "' at " + i);
            i++;
        }

        static string Str(string s, ref int i)
        {
            Expect(s, ref i, '"');
            var b = new StringBuilder();
            while (i < s.Length && s[i] != '"')
            {
                char c = s[i++];
                if (c != '\\')
                {
                    b.Append(c);
                    continue;
                }
                char e = s[i++];
                switch (e)
                {
                    case 'n': b.Append('\n'); break;
                    case 't': b.Append('\t'); break;
                    case 'r': b.Append('\r'); break;
                    case 'b': b.Append('\b'); break;
                    case 'f': b.Append('\f'); break;
                    case 'u':
                        b.Append((char)Convert.ToInt32(s.Substring(i, 4), 16));
                        i += 4;
                        break;
                    default: b.Append(e); break;
                }
            }
            Expect(s, ref i, '"');
            return b.ToString();
        }

        static double Num(string s, ref int i)
        {
            int from = i;
            while (i < s.Length && "+-0123456789.eE".IndexOf(s[i]) >= 0)
                i++;
            if (i == from)
                throw new FormatException("not a JSON value at " + from);
            return double.Parse(s.Substring(from, i - from), NumberStyles.Float, CultureInfo.InvariantCulture);
        }
    }

    public static class JsonGet
    {
        public static object Get(this Dictionary<string, object> d, string key)
        {
            object v;
            return d != null && d.TryGetValue(key, out v) ? v : null;
        }

        public static string GetString(this Dictionary<string, object> d, string key, string def = "")
        {
            var v = d.Get(key) as string;
            return v ?? def;
        }

        public static double GetNumber(this Dictionary<string, object> d, string key, double def = 0)
        {
            object v = d.Get(key);
            return v is double ? (double)v : def;
        }

        public static long GetId(this Dictionary<string, object> d, string key)
        {
            return (long)d.GetNumber(key, 0);
        }

        public static bool GetBool(this Dictionary<string, object> d, string key, bool def = false)
        {
            object v = d.Get(key);
            return v is bool ? (bool)v : def;
        }

        public static Dictionary<string, object> GetDict(this Dictionary<string, object> d, string key)
        {
            return d.Get(key) as Dictionary<string, object>;
        }

        public static List<object> GetList(this Dictionary<string, object> d, string key)
        {
            return d.Get(key) as List<object> ?? new List<object>();
        }

        /// <summary>A list of numbers as floats (missing entries take `def`).</summary>
        public static float[] GetFloats(this Dictionary<string, object> d, string key, params float[] def)
        {
            var list = d.Get(key) as List<object>;
            var r = (float[])def.Clone();
            if (list == null)
                return r;
            if (r.Length < list.Count)
                Array.Resize(ref r, list.Count);
            for (int i = 0; i < list.Count; i++)
                if (list[i] is double)
                    r[i] = (float)(double)list[i];
            return r;
        }
    }
}
