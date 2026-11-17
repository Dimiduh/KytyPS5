using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;

namespace KytyLauncher {

// launcher-settings.json next to the scripts: the same file and keys as launcher.ps1 writes, so both launchers share the
// choices. Keys this launcher does not know are kept as they were.
sealed class Settings {
	// launcher.ps1's settings with its defaults (its order is the file's order), then this launcher's own.
	static readonly object[][] Defaults = {
		new object[] {"game", ""}, new object[] {"data", ""}, new object[] {"resolution", "1920x1080"},
		new object[] {"fullscreen", false}, new object[] {"stretch", false}, new object[] {"language", 1},
		new object[] {"gpu", -1}, new object[] {"gpu_name", ""}, new object[] {"present_mode", "Mailbox"},
		new object[] {"hdr", false}, new object[] {"perf_overlay", false}, new object[] {"sound", true},
		new object[] {"skip_notice_screen", false}, new object[] {"redzone", false},
		new object[] {"precompile_pipelines", true}, new object[] {"pipeline_lists", true},
		new object[] {"shader_pairs", true},
		new object[] {"skip_pending_draws", true}, new object[] {"framegen", 1}, new object[] {"render_scale", 100},
		new object[] {"streamline", ""}, new object[] {"log", "off"}, new object[] {"extra_args", ""},
		new object[] {"environment", ""},
		// (This launcher's own: the window's language, Advanced page; the precompile on the efficiency cores, Shaders page.)
		new object[] {"ui_language", ""}, new object[] {"reopen_launcher", false}, new object[] {"ecores", false},
	};

	readonly string path;
	readonly List<string> order = new List<string>();
	readonly Dictionary<string, object> values = new Dictionary<string, object>();

	// `root`: the folder of run-windows.ps1, which keeps the last game given to it in game-path.txt.
	public Settings(string path, string root) {
		this.path = path;
		foreach (var pair in Defaults) Put((string)pair[0], pair[1]);
		try {
			if (File.Exists(path)) {
				// (UTF-8, with or without a byte order mark.)
				var parsed = new JavaScriptSerializer().DeserializeObject(File.ReadAllText(path, Encoding.UTF8)) as Dictionary<string, object>;
				if (parsed != null) {
					foreach (var pair in parsed) Put(pair.Key, pair.Value);
				}
			}
		} catch (Exception) {
			// An unreadable file: the defaults (it is written again on the next save).
		}
		var last = Path.Combine(root ?? ".", "game-path.txt");
		if (!Bool("game") && File.Exists(last)) {
			try {
				using (var reader = new StreamReader(last)) Put("game", (reader.ReadLine() ?? "").Trim());
			} catch (Exception) {
			}
		}
	}

	void Put(string key, object value) {
		if (!values.ContainsKey(key)) order.Add(key);
		values[key] = value;
	}

	public string Str(string key) {
		object value;
		return values.TryGetValue(key, out value) && value != null ? Convert.ToString(value, CultureInfo.InvariantCulture) : "";
	}

	// As launcher.ps1 takes a value for a switch: true, a number other than 0, text that is not empty.
	public bool Bool(string key) {
		object value;
		if (!values.TryGetValue(key, out value) || value == null) return false;
		if (value is bool) return (bool)value;
		if (value is string) return ((string)value).Length != 0;
		try {
			return Convert.ToDouble(value, CultureInfo.InvariantCulture) != 0;
		} catch (Exception) {
			return true;
		}
	}

	// A number, 0 for anything else (true is 1).
	public int Int(string key) {
		object value;
		if (!values.TryGetValue(key, out value) || value == null) return 0;
		if (value is bool) return (bool)value ? 1 : 0;
		int number;
		if (value is string) return int.TryParse((string)value, NumberStyles.Integer, CultureInfo.InvariantCulture, out number) ? number : 0;
		try {
			return Convert.ToInt32(value, CultureInfo.InvariantCulture);
		} catch (Exception) {
			return 0;
		}
	}

	// A whole number as text or as a number; false for anything else.
	public bool TryInt(string key, out int number) {
		return int.TryParse(Str(key), NumberStyles.Integer, CultureInfo.InvariantCulture, out number);
	}

	public void Set(string key, object value) {
		Put(key, value);
	}

	// (Tests: --dry-run and --screenshot leave the file as it is.)
	public bool ReadOnly;

	// With a byte order mark: Windows PowerShell 5.1 (launcher.ps1) reads a file without one as the ANSI code page, and a
	// game folder or Streamline path with other letters than English ones would come out wrong.
	public void Save() {
		if (ReadOnly) return;
		var text = new StringBuilder();
		text.Append("{\r\n");
		for (int i = 0; i < order.Count; i++) {
			text.Append("    ").Append(Quote(order[i])).Append(": ").Append(Value(values[order[i]]));
			text.Append(i + 1 < order.Count ? ",\r\n" : "\r\n");
		}
		text.Append("}\r\n");
		try {
			File.WriteAllText(path, text.ToString(), new UTF8Encoding(true));
		} catch (Exception) {
			// A read-only folder: the choices last until the window closes.
		}
	}

	static string Value(object value) {
		if (value == null) return "null";
		if (value is bool) return (bool)value ? "true" : "false";
		if (value is string) return Quote((string)value);
		if (value is int || value is long || value is short || value is byte) return Convert.ToString(value, CultureInfo.InvariantCulture);
		if (value is double || value is float || value is decimal)
			return Convert.ToDouble(value, CultureInfo.InvariantCulture).ToString("R", CultureInfo.InvariantCulture);
		// Lists and objects of a newer launcher: as the serializer writes them.
		return new JavaScriptSerializer().Serialize(value);
	}

	static string Quote(string text) {
		var quoted = new StringBuilder(text.Length + 2);
		quoted.Append('"');
		foreach (var c in text) {
			switch (c) {
				case '"': quoted.Append("\\\""); break;
				case '\\': quoted.Append("\\\\"); break;
				case '\n': quoted.Append("\\n"); break;
				case '\r': quoted.Append("\\r"); break;
				case '\t': quoted.Append("\\t"); break;
				default:
					if (c < 0x20) quoted.AppendFormat(CultureInfo.InvariantCulture, "\\u{0:x4}", (int)c);
					else quoted.Append(c);
					break;
			}
		}
		quoted.Append('"');
		return quoted.ToString();
	}
}

}
