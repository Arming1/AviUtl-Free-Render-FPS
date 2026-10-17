using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;

[assembly: AssemblyTitle("AviUtl2 FreeRenderFPS Installer")]
[assembly: AssemblyDescription("Audited offline release-candidate installer for x264guiEx FreeRenderFPS")]
[assembly: AssemblyProduct("AviUtl2 FreeRenderFPS")]
[assembly: AssemblyVersion("1.2.0.0")]
[assembly: AssemblyFileVersion("1.2.0.0")]
[assembly: AssemblyInformationalVersion("1.2.0")]

namespace FreeRenderFPS.Installer;

internal static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            var options = Options.Parse(args);
            if (options.Help)
            {
                Options.PrintHelp();
                return 0;
            }

            if (options.SelfTest)
                return Installer.RunSelfTest(options);

            var target = Installer.ResolveInstallDirectory(options);
            using var log = new InstallerLog(options.LogPath, target);
            log.Info($"FreeRenderFPS installer {typeof(Program).Assembly.GetName().Version ?? new Version(1, 0)}");
            log.Info($"Command={options.Command}; target={target}");

            if (options.CleanupLegacyBackups)
            {
                Installer.CleanupLegacyBackups(target, options, log);
            }
            else if (options.Command is "install" or "upgrade")
            {
                var source = Installer.ResolveSourceDirectory(options);
                Installer.Install(source, target, options, log);
            }
            else if (options.Command == "uninstall")
            {
                Installer.Uninstall(target, options, log);
            }
            else if (options.Command == "rollback")
            {
                Installer.Rollback(target, options, log);
            }
            else
            {
                throw new InstallerException($"Unknown command '{options.Command}'.");
            }

            log.Info("Completed successfully.");
            PauseWhenLaunchedWithoutArguments(args);
            return 0;
        }
        catch (InstallerException ex)
        {
            Console.Error.WriteLine("FreeRenderFPS installer: " + ex.Message);
            PauseWhenLaunchedWithoutArguments(args);
            return 2;
        }
        catch (UnauthorizedAccessException ex)
        {
            Console.Error.WriteLine("FreeRenderFPS installer: access denied. Run the installer elevated or choose a writable test path.");
            Console.Error.WriteLine(ex.Message);
            PauseWhenLaunchedWithoutArguments(args);
            return 5;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("FreeRenderFPS installer failed: " + ex);
            PauseWhenLaunchedWithoutArguments(args);
            return 1;
        }
    }

    private static void PauseWhenLaunchedWithoutArguments(string[] args)
    {
        if (args.Length == 0 && Environment.UserInteractive)
        {
            Console.WriteLine();
            Console.WriteLine("Press Enter to close this installer.");
            Console.ReadLine();
        }
    }
}

internal sealed class InstallerException : Exception
{
    public InstallerException(string message) : base(message) { }
    public InstallerException(string message, Exception inner) : base(message, inner) { }
}

internal sealed class Options
{
    public string Command { get; private set; } = "install";
    public string? Source { get; private set; }
    public string? InstallDirectory { get; private set; }
    public string? TestRoot { get; private set; }
    public string? LogPath { get; private set; }
    public string? BackupPath { get; private set; }
    public bool PreserveConfig { get; private set; } = true;
    public bool PreservePresets { get; private set; } = true;
    public bool NonInteractive { get; private set; }
    public bool DryRun { get; private set; }
    public bool SelfTest { get; private set; }
    public bool KeepTestRoot { get; private set; }
    public bool CleanupLegacyBackups { get; private set; }
    public bool Help { get; private set; }

    public static Options Parse(string[] args)
    {
        var result = new Options();
        var index = 0;
        var hasCommand = false;
        if (args.Length > 0 && !args[0].StartsWith("-", StringComparison.Ordinal))
        {
            result.Command = args[0].ToLowerInvariant();
            hasCommand = true;
            index = 1;
        }

        while (index < args.Length)
        {
            var arg = args[index++];
            switch (arg.ToLowerInvariant())
            {
                case "-h":
                case "--help":
                case "/?":
                    result.Help = true;
                    break;
                case "--self-test":
                    result.SelfTest = true;
                    result.NonInteractive = true;
                    break;
                case "--source":
                    result.Source = ReadValue(args, ref index, arg);
                    break;
                case "--install-dir":
                case "--target":
                    result.InstallDirectory = ReadValue(args, ref index, arg);
                    break;
                case "--test-root":
                    result.TestRoot = ReadValue(args, ref index, arg);
                    break;
                case "--log":
                    result.LogPath = ReadValue(args, ref index, arg);
                    break;
                case "--backup":
                    result.BackupPath = ReadValue(args, ref index, arg);
                    break;
                case "--preserve-config":
                    result.PreserveConfig = true;
                    break;
                case "--preserve-presets":
                    result.PreservePresets = true;
                    break;
                case "--remove-user-data":
                    result.PreserveConfig = false;
                    result.PreservePresets = false;
                    break;
                case "--non-interactive":
                    result.NonInteractive = true;
                    break;
                case "--dry-run":
                    result.DryRun = true;
                    result.NonInteractive = true;
                    break;
                case "--keep-test-root":
                    result.KeepTestRoot = true;
                    break;
                case "--cleanup-legacy-backups":
                    result.CleanupLegacyBackups = true;
                    break;
                default:
                    throw new InstallerException($"Unknown option '{arg}'. Use --help for usage.");
            }
        }

        if (result.CleanupLegacyBackups && hasCommand)
            throw new InstallerException("--cleanup-legacy-backups cannot be combined with a command.");
        if (!new[] { "install", "upgrade", "uninstall", "rollback" }.Contains(result.Command)
            && !result.SelfTest && !result.Help)
            throw new InstallerException($"Unknown command '{result.Command}'. Use --help for usage.");
        return result;
    }

    private static string ReadValue(string[] args, ref int index, string option)
    {
        if (index >= args.Length || args[index].StartsWith("-", StringComparison.Ordinal))
            throw new InstallerException($"{option} requires a value.");
        return args[index++];
    }

    public static void PrintHelp()
    {
        Console.WriteLine("FreeRenderFPS Installer 1.2.0 (audited offline runtime)");
        Console.WriteLine();
        Console.WriteLine("Usage:");
        Console.WriteLine("  FreeRenderFPS.Installer.exe [install|upgrade] [options]");
        Console.WriteLine("  FreeRenderFPS.Installer.exe uninstall [options]");
        Console.WriteLine("  FreeRenderFPS.Installer.exe rollback [options]");
        Console.WriteLine("  FreeRenderFPS.Installer.exe --cleanup-legacy-backups [options]");
        Console.WriteLine("  FreeRenderFPS.Installer.exe --self-test [--test-root <dir>]");
        Console.WriteLine();
        Console.WriteLine("The default target is %ProgramData%\\aviutl2\\Plugin\\x264guiEx-FreeRenderFPS.");
        Console.WriteLine("Only the FreeRenderFPS directory is changed; the original x264guiEx directory is rejected.");
        Console.WriteLine("Backups, staging and swap folders live in %ProgramData%\\aviutl2\\FreeRenderFPS-backups,");
        Console.WriteLine("outside Plugin, so AviUtl2 never loads them as plugins.");
        Console.WriteLine("Run elevated only when the selected target is not writable by the current user.");
        Console.WriteLine();
        Console.WriteLine("Options:");
        Console.WriteLine("  --source <dir>          Package payload or plugin payload directory");
        Console.WriteLine("  --install-dir <dir>     Override target (must end in x264guiEx-FreeRenderFPS)");
        Console.WriteLine("  --preserve-config       Keep existing .conf files on upgrade/uninstall");
        Console.WriteLine("  --preserve-presets      Keep existing .stg/x264guiEx_stg files");
        Console.WriteLine("  --remove-user-data      Remove FreeRenderFPS config/presets on uninstall");
        Console.WriteLine("  --log <file>            Write an installer log to this path");
        Console.WriteLine("  --backup <dir>          Rollback backup directory (rollback only)");
        Console.WriteLine("  --cleanup-legacy-backups  Move old Plugin\\x264guiEx-FreeRenderFPS.backup-* folders into");
        Console.WriteLine("                          FreeRenderFPS-backups (moves only, never deletes; opt-in)");
        Console.WriteLine("  --non-interactive       Never prompt; suitable for deployment scripts");
        Console.WriteLine("  --dry-run               Validate and report without changing files");
        Console.WriteLine("  --test-root <dir>       Use an isolated root for tests");
        Console.WriteLine("  --keep-test-root        Keep the sandbox after --self-test");
    }
}

internal sealed class InstallerLog : IDisposable
{
    private readonly StreamWriter _writer;
    public string Path { get; }

    public InstallerLog(string? requestedPath, string target)
    {
        Path = requestedPath ?? DefaultLogPath(target);
        var parent = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(Path));
        if (!string.IsNullOrWhiteSpace(parent))
            Directory.CreateDirectory(parent);
        _writer = new StreamWriter(new FileStream(Path, FileMode.Append, FileAccess.Write, FileShare.Read), new System.Text.UTF8Encoding(false))
        {
            AutoFlush = true
        };
        Info("Log started: " + Path);
    }

    private static string DefaultLogPath(string target)
    {
        var parent = Directory.GetParent(target)?.FullName;
        if (!string.IsNullOrWhiteSpace(parent))
            return System.IO.Path.Combine(parent, "FreeRenderFPS-install.log");
        return System.IO.Path.Combine(System.IO.Path.GetTempPath(), "FreeRenderFPS-install-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + ".log");
    }

    public void Info(string message) => Write("INFO", message);
    public void Warn(string message) => Write("WARN", message);
    public void Error(string message) => Write("ERROR", message);

    private void Write(string level, string message)
    {
        var line = $"{DateTimeOffset.Now:O} [{level}] {message}";
        Console.WriteLine(line);
        _writer.WriteLine(line);
    }

    public void Dispose() => _writer.Dispose();
}

internal static class Installer
{
    private const string ProductDirectoryName = "x264guiEx-FreeRenderFPS";
    private const string OriginalDirectoryName = "x264guiEx";
    // Sibling of the Plugin folder. AviUtl2 scans Plugin and one level below it,
    // so nothing the installer writes here can be loaded as a duplicate plugin.
    private const string BackupRootName = "FreeRenderFPS-backups";
    private const string BackupPattern = ProductDirectoryName + ".backup-*";
    private const string PluginBinaryName = "x264guiEx-FreeRenderFPS.aux2";
    // Pre-v2 AviUtl2 output plugin. Only legacy rollback backups may still contain it.
    private const string LegacyPluginBinaryName = "x264guiEx-FreeRenderFPS.auo2";
    private static readonly string[] RequiredSidecarFiles =
    {
        "x264guiEx-FreeRenderFPS.ini",
        "x264guiEx-FreeRenderFPS.en.ini",
        "x264guiEx-FreeRenderFPS.zh.ini"
    };
    private static readonly string[] RequiredPluginFiles = new[] { PluginBinaryName }.Concat(RequiredSidecarFiles).ToArray();
    private static readonly IReadOnlyDictionary<string, string> AuditedRuntimeHashes =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
    {
        ["x264.exe"] = "52102BAF1A286FAFA2538D23CF409705114F85A340FFC91D2FB4AB49345FD30A",
        ["ffmpeg.exe"] = "BC5C8D78B67ACEA262586F943DC54F531ABC0F9A78312DC31E4015F73FB091B9",
        ["muxer.exe"] = "64842603E5D1C7D42AA4B8A254F11E09D08DF3289D6C908988A965650CEE57D5",
        ["remuxer.exe"] = "B6B875F36FC48FD85A1952850054F49AA6B978F10AF84FBECCA9D0B06377F5FE",
        ["timelineeditor.exe"] = "F4180B51D5843EF9100C7D420906FE6029A115F32CE2CCA37A3AFFE24BB73F50"
    };
    private static bool _selfTestAllowsRuntimePlaceholders;

    public static string ResolveInstallDirectory(Options options)
    {
        string target;
        if (!string.IsNullOrWhiteSpace(options.InstallDirectory))
            target = options.InstallDirectory!;
        else if (!string.IsNullOrWhiteSpace(options.TestRoot))
            target = System.IO.Path.Combine(options.TestRoot!, "aviutl2", "Plugin", ProductDirectoryName);
        else
        {
            var programData = Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData);
            if (string.IsNullOrWhiteSpace(programData))
                throw new InstallerException("%ProgramData% could not be resolved.");
            target = System.IO.Path.Combine(programData, "aviutl2", "Plugin", ProductDirectoryName);
        }

        target = System.IO.Path.GetFullPath(target);
        EnsureSafeTarget(target);
        return target;
    }

    public static string ResolveSourceDirectory(Options options)
    {
        var source = options.Source;
        if (string.IsNullOrWhiteSpace(source))
            source = System.IO.Path.Combine(AppContext.BaseDirectory, "payload");
        source = System.IO.Path.GetFullPath(source);
        if (!Directory.Exists(source))
            throw new InstallerException($"Payload source does not exist: {source}");
        return source;
    }

    public static void Install(string source, string target, Options options, InstallerLog log)
    {
        EnsureSafeTarget(target);
        var payload = ResolvePluginPayload(source);
        ValidatePluginPayload(payload, allowUserData: false);
        var docs = ResolveDocsPayload(source);
        if (docs is not null)
            ValidateDocsPayload(docs);

        log.Info($"Validated payload: {payload}");
        if (docs is not null)
            log.Info($"Validated documentation payload: {docs}");
        log.Info("Audited runtime set present and SHA-256 verified.");

        if (options.DryRun)
        {
            log.Info("Dry-run requested; no files were changed.");
            return;
        }

        var parent = Directory.GetParent(target)?.FullName ?? throw new InstallerException("Target has no parent directory.");
        Directory.CreateDirectory(parent);
        EnsureNotReparsePoint(parent);
        var workRoot = BackupRoot(target);
        EnsureNotReparsePoint(workRoot);
        var staging = System.IO.Path.Combine(workRoot, "staging-" + Stamp());
        var old = System.IO.Path.Combine(workRoot, "old-" + Stamp());
        string? backup = null;
        try
        {
            Directory.CreateDirectory(staging);
            CopyDirectory(payload, staging);
            if (docs is not null)
                CopyDirectory(docs, System.IO.Path.Combine(staging, "docs"));

            if (Directory.Exists(target))
            {
                EnsureNotReparsePoint(target);
                backup = CreateBackup(target, log);
                MigrateLegacyPluginBinary(target, backup, log);
                Directory.Move(target, old);
            }
            Directory.Move(staging, target);
            if (options.PreserveConfig && Directory.Exists(old))
                CopySelected(old, target, IsConfigFile, log, "config");
            if (options.PreservePresets && Directory.Exists(old))
                CopySelected(old, target, IsPresetFile, log, "preset");
            if (Directory.Exists(old))
                Directory.Delete(old, true);

            log.Info(backup is null ? "Installed new payload." : $"Upgraded payload; backup retained at {backup}");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            TryDeleteDirectory(staging, log);
            if (Directory.Exists(target))
                TryDeleteDirectory(target, log);
            if (Directory.Exists(old))
            {
                Directory.Move(old, target);
                log.Warn("Install failed; restored the pre-install directory.");
            }
            else if (backup is not null && Directory.Exists(backup))
            {
                CopyDirectory(backup, target);
                log.Warn("Install failed; restored the timestamped backup.");
            }
            throw new InstallerException("Install/upgrade failed; rollback was attempted.", ex);
        }
        finally
        {
            TryDeleteDirectory(staging, log);
            TryDeleteDirectory(old, log);
        }
    }

    // The old .auo2 leaves the installed folder with the swap in Install: the new
    // payload never contains it, and the timestamped backup keeps the copy.
    // Logging it here makes the migration visible in the install log.
    private static void MigrateLegacyPluginBinary(string target, string backup, InstallerLog log)
    {
        if (!File.Exists(System.IO.Path.Combine(target, LegacyPluginBinaryName)))
            return;
        log.Info($"Legacy {LegacyPluginBinaryName} found; it is kept in {backup} and is not carried into the new install. Only {PluginBinaryName} is loaded.");
    }

    public static void Uninstall(string target, Options options, InstallerLog log)
    {
        EnsureSafeTarget(target);
        if (!Directory.Exists(target))
        {
            log.Info("Nothing to uninstall; target does not exist.");
            return;
        }
        EnsureNotReparsePoint(target);
        if (options.DryRun)
        {
            log.Info("Dry-run requested; no files were changed.");
            return;
        }

        var backup = CreateBackup(target, log);
        var old = System.IO.Path.Combine(BackupRoot(target), "uninstall-" + Stamp());
        try
        {
            Directory.Move(target, old);
            if (options.PreserveConfig || options.PreservePresets)
            {
                Directory.CreateDirectory(target);
                if (options.PreserveConfig)
                    CopySelected(old, target, IsConfigFile, log, "config");
                if (options.PreservePresets)
                    CopySelected(old, target, IsPresetFile, log, "preset");
                log.Info("Uninstalled runtime files while preserving the requested user data.");
            }
            else
            {
                log.Info("Uninstalled runtime files.");
            }
            Directory.Delete(old, true);
            log.Info($"Uninstall backup retained at {backup}");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            if (Directory.Exists(target))
                TryDeleteDirectory(target, log);
            if (Directory.Exists(old))
                Directory.Move(old, target);
            else if (Directory.Exists(backup))
                CopyDirectory(backup, target);
            throw new InstallerException("Uninstall failed; rollback was attempted.", ex);
        }
    }

    public static void Rollback(string target, Options options, InstallerLog log)
    {
        EnsureSafeTarget(target);
        var backup = options.BackupPath;
        if (string.IsNullOrWhiteSpace(backup))
            backup = FindLatestBackup(target);
        if (string.IsNullOrWhiteSpace(backup) || !Directory.Exists(backup))
            throw new InstallerException("No timestamped backup was found. Use --backup <directory> to select one.");
        backup = System.IO.Path.GetFullPath(backup);
        EnsureNotReparsePoint(backup);
        ValidateRollbackBackup(backup);
        if (options.DryRun)
        {
            log.Info($"Dry-run rollback would restore: {backup}");
            return;
        }

        var currentBackup = Directory.Exists(target) ? CreateBackup(target, log, "rollback-current") : null;
        var old = System.IO.Path.Combine(BackupRoot(target), "rollback-" + Stamp());
        try
        {
            if (Directory.Exists(target))
                Directory.Move(target, old);
            CopyDirectory(backup, target);
            if (Directory.Exists(old))
                Directory.Delete(old, true);
            log.Info($"Rolled back from {backup}");
            if (currentBackup is not null)
                log.Info($"Previous runtime retained at {currentBackup}");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            if (Directory.Exists(target))
                TryDeleteDirectory(target, log);
            if (Directory.Exists(old))
                Directory.Move(old, target);
            throw new InstallerException("Rollback failed; previous runtime was restored if possible.", ex);
        }
    }

    // Opt-in only: moves old Plugin\x264guiEx-FreeRenderFPS.backup-* folders out of the
    // plugin search path. Nothing is deleted; each folder keeps its contents.
    public static void CleanupLegacyBackups(string target, Options options, InstallerLog log)
    {
        EnsureSafeTarget(target);
        var pluginDirectory = Directory.GetParent(target)?.FullName ?? throw new InstallerException("Target has no parent directory.");
        var legacy = Directory.Exists(pluginDirectory)
            ? Directory.EnumerateDirectories(pluginDirectory, BackupPattern, SearchOption.TopDirectoryOnly)
                .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
                .ToList()
            : new List<string>();
        if (legacy.Count == 0)
        {
            log.Info($"No legacy backups found under {pluginDirectory}.");
            return;
        }

        foreach (var path in legacy)
            log.Info($"Legacy backup found: {path}");
        if (options.DryRun)
        {
            log.Info("Dry-run requested; no legacy backups were moved.");
            return;
        }

        var destinationRoot = BackupRoot(target);
        Directory.CreateDirectory(destinationRoot);
        foreach (var source in legacy)
        {
            EnsureNotReparsePoint(source);
            var destination = System.IO.Path.Combine(destinationRoot, System.IO.Path.GetFileName(source) + "-from-plugin-" + Stamp());
            Directory.Move(source, destination);
            log.Info($"Moved legacy backup {source} to {destination}");
        }
    }

    public static int RunSelfTest(Options options)
    {
        _selfTestAllowsRuntimePlaceholders = true;
        var root = options.TestRoot;
        var removeRoot = false;
        if (string.IsNullOrWhiteSpace(root))
        {
            root = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "FreeRenderFPS-selftest-" + Stamp());
            removeRoot = true;
        }
        root = System.IO.Path.GetFullPath(root);
        var payload = System.IO.Path.Combine(root, "payload", "Plugin", ProductDirectoryName);
        var docs = System.IO.Path.Combine(root, "payload", "Docs");
        var pluginDirectory = System.IO.Path.Combine(root, "aviutl2", "Plugin");
        var target = System.IO.Path.Combine(pluginDirectory, ProductDirectoryName);
        var original = System.IO.Path.Combine(pluginDirectory, OriginalDirectoryName);
        var backupRoot = System.IO.Path.Combine(root, "aviutl2", BackupRootName);
        var logPath = System.IO.Path.Combine(root, "self-test.log");
        try
        {
            Directory.CreateDirectory(payload);
            Directory.CreateDirectory(docs);
            foreach (var file in RequiredPluginFiles)
                File.WriteAllText(System.IO.Path.Combine(payload, file), "test-payload-" + file);
            Directory.CreateDirectory(System.IO.Path.Combine(payload, "exe_files"));
            foreach (var runtime in AuditedRuntimeHashes.Keys)
                File.WriteAllText(System.IO.Path.Combine(payload, "exe_files", runtime), "self-test-placeholder");
            File.WriteAllText(System.IO.Path.Combine(payload, "THIRD_PARTY_MANIFEST.json"), "{}");
            File.WriteAllText(System.IO.Path.Combine(payload, "THIRD_PARTY_RELEASE_ARTIFACTS.json"), "{}");
            File.WriteAllText(System.IO.Path.Combine(payload, "THIRD_PARTY_NOTICES.txt"), "self-test notice");
            Directory.CreateDirectory(System.IO.Path.Combine(payload, "x264guiEx_stg"));
            File.WriteAllText(System.IO.Path.Combine(payload, "x264guiEx_stg", "test.stg"), "preset-v1");
            File.WriteAllText(System.IO.Path.Combine(docs, "RUNTIME_DEPENDENCIES.md"), "test notice");
            Directory.CreateDirectory(original);
            File.WriteAllText(System.IO.Path.Combine(original, "must-not-change.txt"), "original");

            using (var log = new InstallerLog(logPath, target))
            {
                var installOptions = OptionsForTest("install", options, preserveConfig: true, preservePresets: true);
                Install(System.IO.Path.Combine(root, "payload"), target, installOptions, log);
            }
            Assert(File.Exists(System.IO.Path.Combine(target, PluginBinaryName)), "initial plugin file missing");
            Assert(BackupDirectories(target).Count == 0, "initial install created a backup");
            AssertPluginBinariesOnlyInTarget(pluginDirectory, target, "after install");

            File.WriteAllText(System.IO.Path.Combine(target, "x264guiEx-FreeRenderFPS.conf"), "user-config");
            File.WriteAllText(System.IO.Path.Combine(target, "x264guiEx_stg", "user.stg"), "user-preset");
            File.WriteAllText(System.IO.Path.Combine(payload, PluginBinaryName), "test-payload-v2");

            using (var log = new InstallerLog(logPath, target))
                Install(System.IO.Path.Combine(root, "payload"), target, OptionsForTest("upgrade", options, true, true), log);
            Assert(File.ReadAllText(System.IO.Path.Combine(target, "x264guiEx-FreeRenderFPS.conf")) == "user-config", "config was not preserved on upgrade");
            Assert(File.ReadAllText(System.IO.Path.Combine(target, "x264guiEx_stg", "user.stg")) == "user-preset", "preset was not preserved on upgrade");
            Assert(File.ReadAllText(System.IO.Path.Combine(original, "must-not-change.txt")) == "original", "original x264guiEx directory changed");
            Assert(BackupDirectories(target).Count == 1, "upgrade backup missing");
            Assert(!Directory.EnumerateDirectories(pluginDirectory, BackupPattern, SearchOption.TopDirectoryOnly).Any(), "upgrade wrote a backup under Plugin");
            AssertNoWorkDirectories(backupRoot, "after upgrade");
            AssertPluginBinariesOnlyInTarget(pluginDirectory, target, "after upgrade");

            using (var log = new InstallerLog(logPath, target))
                Uninstall(target, OptionsForTest("uninstall", options, true, true), log);
            Assert(File.Exists(System.IO.Path.Combine(target, "x264guiEx-FreeRenderFPS.conf")), "config was not preserved on uninstall");
            Assert(!File.Exists(System.IO.Path.Combine(target, PluginBinaryName)), "plugin remained after uninstall");
            AssertPluginBinariesOnlyInTarget(pluginDirectory, target, "after uninstall");

            using (var log = new InstallerLog(logPath, target))
                Rollback(target, OptionsForTest("rollback", options, false, false), log);
            Assert(File.Exists(System.IO.Path.Combine(target, PluginBinaryName)), "rollback did not restore plugin");
            Assert(File.ReadAllText(System.IO.Path.Combine(original, "must-not-change.txt")) == "original", "original x264guiEx directory changed during rollback");
            AssertPluginBinariesOnlyInTarget(pluginDirectory, target, "after rollback");

            // A payload must never ship the legacy .auo2, nor a .aux2 outside the plugin root.
            AssertPayloadRejects(payload, LegacyPluginBinaryName, "payload with legacy auo2 was accepted");
            var nestedProbe = System.IO.Path.Combine("probe-nested", PluginBinaryName);
            AssertPayloadRejects(payload, nestedProbe, "payload with nested aux2 was accepted");
            TryDeleteDirectory(System.IO.Path.Combine(payload, "probe-nested"), null);

            // Migration: a v1.0.x target (legacy .auo2, no .aux2) is upgraded to .aux2.
            // The legacy file must leave the installed folder, stay in a backup outside
            // Plugin, and come back when rolling back to that backup.
            var backupsBeforeMigration = BackupDirectories(target);
            File.Delete(System.IO.Path.Combine(target, PluginBinaryName));
            File.WriteAllText(System.IO.Path.Combine(target, LegacyPluginBinaryName), "legacy-auo2");
            using (var log = new InstallerLog(logPath, target))
                Install(System.IO.Path.Combine(root, "payload"), target, OptionsForTest("upgrade", options, true, true), log);
            Assert(!File.Exists(System.IO.Path.Combine(target, LegacyPluginBinaryName)), "legacy auo2 remained after migration");
            Assert(File.Exists(System.IO.Path.Combine(target, PluginBinaryName)), "aux2 missing after migration");
            var migrationBackups = BackupDirectories(target).Where(path => !backupsBeforeMigration.Contains(path)).ToList();
            Assert(migrationBackups.Count == 1, "migration backup missing");
            Assert(string.Equals(System.IO.Path.GetDirectoryName(migrationBackups[0]), System.IO.Path.GetFullPath(backupRoot), StringComparison.OrdinalIgnoreCase), "migration backup was not created outside Plugin");
            Assert(File.ReadAllText(System.IO.Path.Combine(migrationBackups[0], LegacyPluginBinaryName)) == "legacy-auo2", "legacy auo2 not kept in migration backup");
            AssertPluginBinariesOnlyInTarget(pluginDirectory, target, "after migration");

            using (var log = new InstallerLog(logPath, target))
                Rollback(target, Options.Parse(new[] { "rollback", "--non-interactive", "--backup", migrationBackups[0] }), log);
            Assert(File.ReadAllText(System.IO.Path.Combine(target, LegacyPluginBinaryName)) == "legacy-auo2", "rollback did not restore legacy auo2");
            Assert(!File.Exists(System.IO.Path.Combine(target, PluginBinaryName)), "rollback to legacy backup left aux2 in place");
            AssertPluginBinariesOnlyInTarget(pluginDirectory, target, "after rollback to migration backup");

            // Legacy-location backups (Plugin\x264guiEx-FreeRenderFPS.backup-*) stay usable for
            // rollback, are never touched by a default upgrade, and are moved (not deleted)
            // by --cleanup-legacy-backups.
            var cleanupFlag = Options.Parse(new[] { "--cleanup-legacy-backups" });
            Assert(cleanupFlag.CleanupLegacyBackups, "cleanup flag not parsed");
            var rejectedCombination = false;
            try
            {
                Options.Parse(new[] { "install", "--cleanup-legacy-backups" });
            }
            catch (InstallerException)
            {
                rejectedCombination = true;
            }
            Assert(rejectedCombination, "cleanup flag combined with a command was accepted");

            var legacyBackup = System.IO.Path.Combine(pluginDirectory, ProductDirectoryName + ".backup-20000101-000000000Z-legacy");
            CopyDirectory(target, legacyBackup);
            File.WriteAllText(System.IO.Path.Combine(legacyBackup, "x264guiEx-FreeRenderFPS.conf"), "legacy-marker");
            Assert(File.Exists(System.IO.Path.Combine(legacyBackup, LegacyPluginBinaryName)), "legacy-location backup lacks auo2");

            using (var log = new InstallerLog(logPath, target))
                Install(System.IO.Path.Combine(root, "payload"), target, OptionsForTest("upgrade", options, true, true), log);
            Assert(Directory.Exists(legacyBackup), "default upgrade touched a legacy-location backup");
            Assert(File.Exists(System.IO.Path.Combine(target, PluginBinaryName)), "aux2 missing after upgrade over legacy state");

            using (var log = new InstallerLog(logPath, target))
                Rollback(target, Options.Parse(new[] { "rollback", "--non-interactive", "--backup", legacyBackup }), log);
            Assert(File.ReadAllText(System.IO.Path.Combine(target, LegacyPluginBinaryName)) == "legacy-auo2", "rollback from legacy-location backup did not restore auo2");
            Assert(!File.Exists(System.IO.Path.Combine(target, PluginBinaryName)), "rollback from legacy-location backup left aux2 in place");

            using (var log = new InstallerLog(logPath, target))
                CleanupLegacyBackups(target, Options.Parse(new[] { "--cleanup-legacy-backups", "--non-interactive" }), log);
            Assert(!Directory.Exists(legacyBackup), "legacy-location backup still under Plugin after cleanup");
            var movedBackups = BackupDirectories(target)
                .Where(path => System.IO.Path.GetFileName(path).StartsWith(System.IO.Path.GetFileName(legacyBackup) + "-from-plugin-", StringComparison.OrdinalIgnoreCase))
                .ToList();
            Assert(movedBackups.Count == 1, "cleanup did not move the legacy backup");
            Assert(File.ReadAllText(System.IO.Path.Combine(movedBackups[0], "x264guiEx-FreeRenderFPS.conf")) == "legacy-marker", "moved legacy backup content changed");
            Assert(File.Exists(System.IO.Path.Combine(movedBackups[0], LegacyPluginBinaryName)), "moved legacy backup lost its auo2");
            AssertPluginBinariesOnlyInTarget(pluginDirectory, target, "after legacy cleanup");
            Assert(File.ReadAllText(System.IO.Path.Combine(original, "must-not-change.txt")) == "original", "original x264guiEx directory changed at end of self-test");

            Console.WriteLine("Self-test passed. Sandbox: " + root);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("Self-test failed: " + ex.Message);
            return 1;
        }
        finally
        {
            if (removeRoot && !options.KeepTestRoot)
                TryDeleteDirectory(root, null);
        }
    }

    private static Options OptionsForTest(string command, Options original, bool preserveConfig, bool preservePresets)
    {
        var args = new List<string> { command, "--non-interactive", "--test-root", original.TestRoot ?? System.IO.Path.Combine(System.IO.Path.GetTempPath(), "unused") };
        if (preserveConfig) args.Add("--preserve-config");
        if (preservePresets) args.Add("--preserve-presets");
        var result = Options.Parse(args.ToArray());
        return result;
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InstallerException(message);
    }

    private static void AssertPayloadRejects(string payload, string relativeProbe, string message)
    {
        var probe = System.IO.Path.Combine(payload, relativeProbe);
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(probe)!);
        File.WriteAllText(probe, "must-not-ship");
        bool rejected;
        try
        {
            ValidatePluginPayload(payload, allowUserData: false);
            rejected = false;
        }
        catch (InstallerException)
        {
            rejected = true;
        }
        finally
        {
            File.Delete(probe);
        }
        Assert(rejected, message);
    }

    // Every plugin binary under Plugin must be inside the install folder itself.
    private static void AssertPluginBinariesOnlyInTarget(string pluginDirectory, string target, string stage)
    {
        var targetPrefix = System.IO.Path.GetFullPath(target).TrimEnd(System.IO.Path.DirectorySeparatorChar) + System.IO.Path.DirectorySeparatorChar;
        var offenders = Directory.EnumerateFiles(pluginDirectory, "*", SearchOption.AllDirectories)
            .Where(file => IsPluginBinary(file) && !System.IO.Path.GetFullPath(file).StartsWith(targetPrefix, StringComparison.OrdinalIgnoreCase))
            .ToList();
        Assert(offenders.Count == 0, $"plugin binary outside the install folder ({stage}): {string.Join(", ", offenders)}");
    }

    private static bool IsPluginBinary(string path)
    {
        var extension = System.IO.Path.GetExtension(path);
        return extension.Equals(".aux2", StringComparison.OrdinalIgnoreCase) || extension.Equals(".auo2", StringComparison.OrdinalIgnoreCase);
    }

    private static void AssertNoWorkDirectories(string backupRoot, string stage)
    {
        if (!Directory.Exists(backupRoot))
            return;
        var leftovers = Directory.EnumerateDirectories(backupRoot, "*", SearchOption.TopDirectoryOnly)
            .Where(path => System.IO.Path.GetFileName(path).StartsWith("staging-", StringComparison.OrdinalIgnoreCase) || System.IO.Path.GetFileName(path).StartsWith("old-", StringComparison.OrdinalIgnoreCase))
            .ToList();
        Assert(leftovers.Count == 0, $"work directory left behind ({stage}): {string.Join(", ", leftovers)}");
    }

    private static HashSet<string> BackupDirectories(string target)
    {
        var result = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var root = BackupRoot(target);
        if (!Directory.Exists(root))
            return result;
        foreach (var path in Directory.EnumerateDirectories(root, BackupPattern, SearchOption.TopDirectoryOnly))
            result.Add(path);
        return result;
    }

    private static string ResolvePluginPayload(string source, bool allowLegacyBinary = false)
    {
        var candidates = new[]
        {
            System.IO.Path.Combine(source, "payload", "Plugin", ProductDirectoryName),
            System.IO.Path.Combine(source, "Plugin", ProductDirectoryName),
            System.IO.Path.Combine(source, ProductDirectoryName),
            source
        };
        foreach (var candidate in candidates)
        {
            if (Directory.Exists(candidate) && HasFormalPluginFiles(candidate, allowLegacyBinary))
                return System.IO.Path.GetFullPath(candidate);
        }
        throw new InstallerException($"Payload does not contain the formal Release plugin files ({PluginBinaryName} and adjacent .ini files).");
    }

    private static bool HasFormalPluginFiles(string candidate, bool allowLegacyBinary)
    {
        var hasBinary = File.Exists(System.IO.Path.Combine(candidate, PluginBinaryName))
            || (allowLegacyBinary && File.Exists(System.IO.Path.Combine(candidate, LegacyPluginBinaryName)));
        return hasBinary && RequiredSidecarFiles.All(file => File.Exists(System.IO.Path.Combine(candidate, file)));
    }

    private static string? ResolveDocsPayload(string source)
    {
        var candidates = new[]
        {
            System.IO.Path.Combine(source, "payload", "Docs"),
            System.IO.Path.Combine(source, "Docs"),
            System.IO.Path.Combine(source, "docs")
        };
        return candidates.FirstOrDefault(Directory.Exists);
    }

    private static void ValidatePluginPayload(string payload, bool allowUserData, bool allowLegacyBinary = false)
    {
        foreach (var file in Directory.EnumerateFiles(payload, "*", SearchOption.AllDirectories))
        {
            var relative = GetRelativePath(payload, file).Replace('\\', '/');
            var name = System.IO.Path.GetFileName(file);
            if (allowUserData && relative.StartsWith("docs/", StringComparison.OrdinalIgnoreCase))
                continue;
            if (relative.StartsWith("exe_files/", StringComparison.OrdinalIgnoreCase))
            {
                var runtimeName = relative.Substring("exe_files/".Length);
                if (runtimeName.IndexOf('/') >= 0 || !AuditedRuntimeHashes.ContainsKey(runtimeName))
                    throw new InstallerException($"Payload contains an unaudited runtime file: {relative}");
                if (!allowUserData && !_selfTestAllowsRuntimePlaceholders)
                {
                    var actual = ComputeSha256(file);
                    if (!actual.Equals(AuditedRuntimeHashes[runtimeName], StringComparison.OrdinalIgnoreCase))
                        throw new InstallerException($"Audited runtime SHA-256 mismatch: {relative}");
                }
                continue;
            }
            if (ForbiddenExtension(name))
                throw new InstallerException($"Payload contains a prohibited binary file: {relative}");

            var inPresetDirectory = relative.StartsWith("x264guiEx_stg/", StringComparison.OrdinalIgnoreCase);
            if (inPresetDirectory)
            {
                if (!name.EndsWith(".stg", StringComparison.OrdinalIgnoreCase))
                    throw new InstallerException($"Only .stg files are permitted in x264guiEx_stg: {relative}");
                continue;
            }
            if (allowUserData && IsConfigFile(file))
                continue;
            // The plugin binary is accepted only at the plugin root, by exact name.
            var isPluginBinary = relative.Equals(PluginBinaryName, StringComparison.OrdinalIgnoreCase)
                || (allowLegacyBinary && relative.Equals(LegacyPluginBinaryName, StringComparison.OrdinalIgnoreCase));
            if (!isPluginBinary
                && !RequiredSidecarFiles.Contains(name, StringComparer.OrdinalIgnoreCase)
                && !name.Equals("THIRD_PARTY_MANIFEST.json", StringComparison.OrdinalIgnoreCase)
                && !name.Equals("THIRD_PARTY_RELEASE_ARTIFACTS.json", StringComparison.OrdinalIgnoreCase)
                && !name.Equals("THIRD_PARTY_NOTICES.txt", StringComparison.OrdinalIgnoreCase))
                throw new InstallerException($"Unexpected plugin payload file: {relative}");
        }
        foreach (var runtime in AuditedRuntimeHashes.Keys)
        {
            if (!File.Exists(System.IO.Path.Combine(payload, "exe_files", runtime)))
                throw new InstallerException($"Payload is missing audited runtime: exe_files/{runtime}");
        }
        if (!File.Exists(System.IO.Path.Combine(payload, "THIRD_PARTY_MANIFEST.json"))
            || !File.Exists(System.IO.Path.Combine(payload, "THIRD_PARTY_RELEASE_ARTIFACTS.json"))
            || !File.Exists(System.IO.Path.Combine(payload, "THIRD_PARTY_NOTICES.txt")))
            throw new InstallerException("Payload is missing a required third-party manifest or notice file.");
    }

    private static void ValidateDocsPayload(string docs)
    {
        foreach (var file in Directory.EnumerateFiles(docs, "*", SearchOption.AllDirectories))
        {
            var relative = GetRelativePath(docs, file).Replace('\\', '/');
            var extension = System.IO.Path.GetExtension(file);
            var isKnownNotice = System.IO.Path.GetFileName(file).Equals("LICENSE", StringComparison.OrdinalIgnoreCase);
            if (ForbiddenExtension(file) || (!isKnownNotice && !new[] { ".md", ".txt", ".html", ".pdf" }.Contains(extension, StringComparer.OrdinalIgnoreCase)))
                throw new InstallerException($"Unexpected or prohibited documentation payload file: {relative}");
        }
    }

    private static void ValidateRollbackBackup(string backup)
    {
        // Backups taken before the .aux2 format still hold the legacy .auo2.
        var plugin = ResolvePluginPayload(backup, allowLegacyBinary: true);
        ValidatePluginPayload(plugin, allowUserData: true, allowLegacyBinary: true);
        var docs = ResolveDocsPayload(backup);
        if (docs is not null)
            ValidateDocsPayload(docs);
    }

    private static bool ForbiddenExtension(string path)
    {
        var extension = System.IO.Path.GetExtension(path);
        return new[] { ".exe", ".dll", ".sys", ".ocx", ".so", ".dylib", ".lib", ".exp", ".pdb", ".metagen", ".recipe" }
            .Contains(extension, StringComparer.OrdinalIgnoreCase);
    }

    private static string ComputeSha256(string path)
    {
        using var stream = File.OpenRead(path);
        using var sha = System.Security.Cryptography.SHA256.Create();
        return string.Concat(sha.ComputeHash(stream).Select(value => value.ToString("X2")));
    }

    private static bool IsConfigFile(string path) => System.IO.Path.GetExtension(path).Equals(".conf", StringComparison.OrdinalIgnoreCase);
    private static bool IsPresetFile(string path) => System.IO.Path.GetExtension(path).Equals(".stg", StringComparison.OrdinalIgnoreCase)
        || path.Split(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar).Any(part => part.Equals("x264guiEx_stg", StringComparison.OrdinalIgnoreCase));

    private static void CopySelected(string source, string destination, Func<string, bool> predicate, InstallerLog log, string kind)
    {
        foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            if (!predicate(file))
                continue;
            var relative = GetRelativePath(source, file);
            var target = System.IO.Path.Combine(destination, relative);
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(target)!);
            File.Copy(file, target, true);
            log.Info($"Preserved {kind}: {relative}");
        }
    }

    // Backups, staging and swap folders live beside the Plugin folder, never inside it.
    // For %ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS this is %ProgramData%\aviutl2\FreeRenderFPS-backups.
    private static string BackupRoot(string target)
    {
        var pluginDirectory = Directory.GetParent(target)?.FullName ?? throw new InstallerException("Target has no parent directory.");
        var dataDirectory = Directory.GetParent(pluginDirectory)?.FullName ?? throw new InstallerException("Plugin folder has no parent directory.");
        return System.IO.Path.Combine(dataDirectory, BackupRootName);
    }

    private static string? FindLatestBackup(string target)
    {
        var candidates = new List<string>();
        var root = BackupRoot(target);
        if (Directory.Exists(root))
            candidates.AddRange(Directory.EnumerateDirectories(root, BackupPattern, SearchOption.TopDirectoryOnly));
        // Backups written before the move out of Plugin remain valid rollback sources.
        var pluginDirectory = Directory.GetParent(target)!.FullName;
        if (Directory.Exists(pluginDirectory))
            candidates.AddRange(Directory.EnumerateDirectories(pluginDirectory, BackupPattern, SearchOption.TopDirectoryOnly));
        return candidates.OrderByDescending(path => Directory.GetLastWriteTimeUtc(path)).FirstOrDefault();
    }

    private static string CreateBackup(string target, InstallerLog log, string suffix = "")
    {
        var path = System.IO.Path.Combine(BackupRoot(target), ProductDirectoryName + ".backup-" + Stamp() + (string.IsNullOrEmpty(suffix) ? "" : "-" + suffix));
        CopyDirectory(target, path);
        log.Info("Created timestamped backup: " + path);
        return path;
    }

    private static string Stamp() => DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff'Z'") + "-" + System.Diagnostics.Process.GetCurrentProcess().Id;

    private static void EnsureSafeTarget(string target)
    {
        var info = new DirectoryInfo(target);
        if (info.Name.Equals(OriginalDirectoryName, StringComparison.OrdinalIgnoreCase))
            throw new InstallerException("Refusing to touch the original x264guiEx directory.");
        foreach (var part in target.Split(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar))
        {
            if (part.Equals(OriginalDirectoryName, StringComparison.OrdinalIgnoreCase))
                throw new InstallerException("Refusing a target path containing the original x264guiEx directory.");
        }
        if (!info.Name.Equals(ProductDirectoryName, StringComparison.OrdinalIgnoreCase))
            throw new InstallerException($"Target must end in {ProductDirectoryName}; the original x264guiEx installation is never modified.");
    }

    private static void EnsureNotReparsePoint(string path)
    {
        if (Directory.Exists(path) && new DirectoryInfo(path).Attributes.HasFlag(FileAttributes.ReparsePoint))
            throw new InstallerException("Refusing to operate on a reparse-point directory: " + path);
    }

    private static void CopyDirectory(string source, string destination)
    {
        EnsureNotReparsePoint(source);
        Directory.CreateDirectory(destination);
        foreach (var directory in Directory.EnumerateDirectories(source, "*", SearchOption.AllDirectories))
        {
            var relative = GetRelativePath(source, directory);
            EnsureNotReparsePoint(directory);
            Directory.CreateDirectory(System.IO.Path.Combine(destination, relative));
        }
        foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            var relative = GetRelativePath(source, file);
            var target = System.IO.Path.Combine(destination, relative);
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(target)!);
            File.Copy(file, target, true);
        }
    }

    private static string GetRelativePath(string root, string path)
    {
        var rootFull = System.IO.Path.GetFullPath(root).TrimEnd(System.IO.Path.DirectorySeparatorChar) + System.IO.Path.DirectorySeparatorChar;
        var rootUri = new Uri(rootFull);
        var pathUri = new Uri(System.IO.Path.GetFullPath(path));
        return Uri.UnescapeDataString(rootUri.MakeRelativeUri(pathUri).ToString()).Replace('/', System.IO.Path.DirectorySeparatorChar);
    }

    private static void TryDeleteDirectory(string? path, InstallerLog? log)
    {
        if (string.IsNullOrWhiteSpace(path) || !Directory.Exists(path))
            return;
        try
        {
            Directory.Delete(path, true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            log?.Warn($"Could not remove temporary directory '{path}': {ex.Message}");
        }
    }
}
