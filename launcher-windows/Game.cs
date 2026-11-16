using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;

namespace KytyLauncher {

// A game: its folder (eboot.bin and sce_sys), or a file: that folder packed into a ZArchive (.zar), which the emulator
// reads without extracting it. A folder's param.json says what it is; an archive's only the emulator reads.
sealed class GameInfo {
	public string Path = "";
	public string TitleId = "";
	public string Version = "";
	public string Name = "";
	public string Picture;   // sce_sys\pic0.png or pic1.png of the dump (the game's own art, read from the user's files)
	public string Icon;      // sce_sys\icon0.png
	public bool Folder;      // a folder with eboot.bin
	public bool File;        // a file (a .zar archive, or an ELF)
	public long Size;        // the file's size in bytes

	// What run-windows.ps1 takes as a game (its Test-Game).
	public bool Found { get { return Folder || File; } }
	// param.json was read: the title and version are known.
	public bool Described { get { return TitleId.Length != 0; } }
	public bool Archive { get { return File && Path.EndsWith(".zar", StringComparison.OrdinalIgnoreCase); } }

	// The name to show: the title, else the title id, else the file's or folder's name.
	public string Title {
		get {
			if (Name.Length != 0) return Name;
			if (TitleId.Length != 0) return TitleId;
			try {
				return System.IO.Path.GetFileName(Path.TrimEnd('\\', '/'));
			} catch (Exception) {
				return Path;
			}
		}
	}

	public static GameInfo Read(string path) {
		var info = new GameInfo { Path = (path ?? "").Trim().Trim('"') };
		if (info.Path.Length == 0) return info;
		Dictionary<string, object> param = null;
		try {
			if (System.IO.File.Exists(info.Path)) {
				info.File = true;
				info.Size = new FileInfo(info.Path).Length;
				return info;
			}
			if (!Directory.Exists(info.Path)) return info;
			info.Folder = System.IO.File.Exists(System.IO.Path.Combine(info.Path, "eboot.bin"));
			var file = System.IO.Path.Combine(info.Path, "sce_sys", "param.json");
			if (System.IO.File.Exists(file)) {
				param = new JavaScriptSerializer { MaxJsonLength = 64 * 1024 * 1024 }
					.DeserializeObject(System.IO.File.ReadAllText(file, Encoding.UTF8)) as Dictionary<string, object>;
				foreach (var name in new[] { "pic0.png", "pic1.png" }) {
					var picture = System.IO.Path.Combine(info.Path, "sce_sys", name);
					if (System.IO.File.Exists(picture)) {
						info.Picture = picture;
						break;
					}
				}
				var icon = System.IO.Path.Combine(info.Path, "sce_sys", "icon0.png");
				if (System.IO.File.Exists(icon)) info.Icon = icon;
			}
		} catch (Exception) {
		}
		if (param == null) return info;
		info.TitleId = Text(param, "titleId");
		info.Version = Text(param, "contentVersion");
		var localized = param.ContainsKey("localizedParameters") ? param["localizedParameters"] as Dictionary<string, object> : null;
		if (localized != null) {
			var language = Text(localized, "defaultLanguage");
			var names = language.Length != 0 && localized.ContainsKey(language) ? localized[language] as Dictionary<string, object> : null;
			if (names != null) info.Name = Text(names, "titleName");
		}
		return info;
	}

	static string Text(Dictionary<string, object> map, string key) {
		object value;
		return map.TryGetValue(key, out value) && value != null ? Convert.ToString(value) : "";
	}
}

}
