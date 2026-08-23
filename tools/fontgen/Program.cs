using System.Buffers.Binary;
using System.IO.Compression;
using System.Text;

namespace fontgen
{
    /// <summary>
    /// Generates the OpenRE binary font files (.fnt) from the RE2 font atlas
    /// TIMs. The .fnt format stores explicit per-character records (unicode
    /// codepoint, texture index, kerning, source rect) so the engine no longer
    /// needs implicit grid math.
    ///
    /// Glyph metrics for A-Za-z0-9 are measured automatically from a decoded
    /// atlas image (--png), giving every glyph a uniform gap (--gap) between
    /// ink columns. Other characters fall back to the extracted width_tbl.
    ///
    /// usage: fontgen [--data &lt;re2 data dir&gt;] [--out &lt;output dir&gt;]
    ///                [--png &lt;atlas png&gt;] [--gap &lt;pixels&gt;]
    /// </summary>
    internal class Program
    {
        private const uint Magic = 0x544E464F; // 'OFNT'

        private const int HeaderSize = 32;
        private const int TextureEntrySize = 16;
        private const int CharRecordSize = 40;

        private static string DefaultPngPath = @"C:\Users\Ted\Desktop\font.png";

        // Original kerning table (int8 pairs {left, right}) extracted from
        // width_tbl at 0x0052E478 in bio2 1.10.exe. Indexed by atlas cell -
        // 36; cell 36 (space) is pair 0.
        private static readonly sbyte[] WidthTbl =
        {
            0, 8, // space
            2, 5, // .
            4, 7, // right arrow
            4, 6, // 「
            5, 6, // 」
            5, 7, // (
            3, 7, // )
            3, 8, // 『
            4, 7, // 』
            6, 8, // “
            1, 8, // (unidentified)
            2, 10, // ▼
            1, 12, 1, 12, 1, 12, 1, 12, 1, 12, 1, 12, 1, 12, 1, 12, 1, 12, 1, 12, // 0-9
            4, 7, // :
            0, 14, // 、
            2, 5, // ,
            2, 11, // ”
            4, 6, // !
            2, 10, // ?
            1, 13, // ⁉
            0, 14, // A
            1, 13, // B
            1, 12, // C
            1, 12, // D
            1, 12, // E
            2, 11, // F
            1, 13, // G
            0, 14, // H
            4, 7, // I
            1, 13, // J
            1, 13, // K
            1, 13, // L
            0, 15, // M
            0, 14, // N
            1, 12, // O
            1, 12, // P
            0, 13, // Q
            0, 14, // R
            1, 12, // S
            1, 13, // T
            1, 13, // U
            1, 13, // V
            0, 15, // W
            1, 13, // X
            1, 13, // Y
            1, 13, // Z
            3, 8, // [
            0, 15, // /
            3, 8, // ]
            4, 7, // ’
            0, 14, // -
            0, 14, // •
            2, 10, // a
            2, 10, // b
            3, 9, // c
            2, 10, // d
            3, 9, // e
            3, 9, // f
            2, 10, // g
            2, 11, // h
            4, 6, // i
            4, 6, // j
            2, 11, // k
            4, 6, // l
            0, 14, // m
            2, 11, // n
            2, 10, // o
            2, 10, // p
            2, 10, // q
            3, 8, // r
            3, 9, // s
            3, 8, // t
            2, 11, // u
            2, 11, // v
            0, 14, // w
            2, 11, // x
            2, 11, // y
            3, 9, // z
            0, 0,
        };

        // Hand-tuned metric overrides {left, right} by codepoint, applied on
        // top of everything else. Kept empty - glyph metrics are measured
        // automatically from the reference PNG for A-Za-z0-9.
        private static readonly Dictionary<uint, (int Left, int Right)> MetricOverrides = new();

        private sealed class Glyph
        {
            public uint Codepoint;
            public int Cell;
            public int Left;
            public int Right;
        }

        private sealed class FontDef
        {
            public required string OutputName;
            public required string TexturePath;
            public int DefaultLeft;
            public int DefaultRight;
            public int DefaultTop;
            public int DefaultBottom;
            public int CellSize;
            public int CellCols;
            public bool Kerning;
            public required List<Glyph> Characters;
        }

        private string _dataDir = string.Empty;
        private int _gap = 1;
        private InkMask? _ink;

        public static int Main(string[] args)
        {
            return new Program().Run(args);
        }

        private int Run(string[] args)
        {
            string? dataDir = null;
            string? outDir = null;
            string? pngPath = null;
            for (int i = 0; i < args.Length; i++)
            {
                switch (args[i])
                {
                    case "--data":
                        dataDir = args[++i];
                        break;
                    case "--out":
                        outDir = args[++i];
                        break;
                    case "--png":
                        pngPath = args[++i];
                        break;
                    case "--gap":
                        if (!int.TryParse(args[++i], out _gap) || _gap < 0)
                        {
                            Console.Error.WriteLine("--gap must be a non-negative integer");
                            return 1;
                        }
                        break;
                    default:
                        Console.Error.WriteLine($"unknown argument '{args[i]}'");
                        return 1;
                }
            }

            dataDir ??= Environment.GetEnvironmentVariable("OPENRE_RE2_DATA");
            if (string.IsNullOrEmpty(dataDir))
            {
                Console.Error.WriteLine("no data dir: pass --data or set OPENRE_RE2_DATA");
                return 1;
            }
            _dataDir = dataDir;

            outDir ??= Path.Combine(Directory.GetCurrentDirectory(), "games", "re2", "fonts");
            Directory.CreateDirectory(outDir);

            // Load the reference atlas image used to measure glyph ink. If it
            // is unavailable we fall back to the extracted width_tbl metrics.
            pngPath ??= DefaultPngPath;
            if (File.Exists(pngPath))
            {
                _ink = LoadPngTransparency(pngPath);
                Console.WriteLine($"measured glyph ink from {pngPath} (gap = {_gap}px)");
            }
            else
            {
                Console.WriteLine($"note: '{pngPath}' not found; using width_tbl metrics");
            }

            var fonts = new List<FontDef> { BuildStdFont(), BuildSmallFont() };
            foreach (var font in fonts)
            {
                if (!ValidateTexture(font.TexturePath))
                    return 2;
                var path = Path.Combine(outDir, font.OutputName);
                WriteFont(path, font);
                Console.WriteLine(
                    $"{font.OutputName}: {font.Characters.Count} chars -> {path}");
            }
            return 0;
        }

        /// <summary>
        /// The standard (large) font: 14px cells, 18 per row on font0.tim.
        /// Rows 0-1 of the atlas are empty; cells 36-47 hold a handful of
        /// symbols. From '0' (cell 48) the atlas is ASCII-aligned for the
        /// digits and letters, but the punctuation runs differ from ASCII:
        /// between '9' and 'A' sit : 、 , ” ! ? ⁉, and between 'Z' and 'a'
        /// sit [ / ] ’ - •. Metrics for A-Za-z0-9 are measured from the
        /// reference PNG when available so every glyph gets a uniform ink
        /// gap; other characters use the extracted width_tbl values.
        /// </summary>
        private FontDef BuildStdFont()
        {
            var characters = new List<Glyph>();
            void Add(uint codepoint, int cell)
            {
                var glyph = new Glyph { Codepoint = codepoint, Cell = cell };
                if (MetricOverrides.TryGetValue(codepoint, out var overridden))
                {
                    glyph.Left = overridden.Left;
                    glyph.Right = overridden.Right;
                }
                else if (TryMeasureMetrics(codepoint, cell, 14, 18, out var measured))
                {
                    glyph.Left = measured.Left;
                    glyph.Right = measured.Right;
                    Console.WriteLine(
                        "  {0} U+{1:X4}: left = {2}, right = {3}",
                        char.IsAsciiLetterOrDigit((char)codepoint) ? $"'{(char)codepoint}'" : "??",
                        codepoint,
                        measured.Left,
                        measured.Right);
                }
                else if (TryGetKerning(cell, out var left, out var right))
                {
                    glyph.Left = left;
                    glyph.Right = right;
                }
                else
                {
                    glyph.Left = 0;
                    glyph.Right = 14;
                }
                characters.Add(glyph);
            }

            // Symbols occupying the first 12 cells of row 2. (Cell 46 is an
            // unidentified symbol; it is left unmapped until identified.)
            Add(0x20, 36); // space
            Add(0x2E, 37); // .
            Add(0x1F782, 38); // right arrow
            Add(0x300C, 39); // 「
            Add(0x300D, 40); // 」
            Add(0x28, 41); // (
            Add(0x29, 42); // )
            Add(0x300E, 43); // 『
            Add(0x300F, 44); // 』
            Add(0x201C, 45); // “
            Add(0x25BC, 47); // ▼

            // Digits: ASCII-aligned with the atlas cells.
            for (uint cp = 0x30; cp <= 0x39; cp++)
                Add(cp, (int)cp);

            // Symbols between '9' and 'A'.
            Add(0x3A, 58); // :
            Add(0x3001, 59); // 、(\-shaped comma)
            Add(0x2C, 60); // ,
            Add(0x201D, 61); // ”
            Add(0x21, 62); // !
            Add(0x3F, 63); // ?
            Add(0x2049, 64); // ⁉

            // Upper-case letters: ASCII-aligned with the atlas cells.
            for (uint cp = 0x41; cp <= 0x5A; cp++)
                Add(cp, (int)cp);

            // Symbols between 'Z' and 'a'.
            Add(0x5B, 91); // [
            Add(0x2F, 92); // /
            Add(0x5D, 93); // ]
            Add(0x2019, 94); // ’
            Add(0x2D, 95); // -
            Add(0x2022, 96); // •

            // Lower-case letters: ASCII-aligned with the atlas cells.
            for (uint cp = 0x61; cp <= 0x7A; cp++)
                Add(cp, (int)cp);

            characters.Sort((a, b) => a.Codepoint.CompareTo(b.Codepoint));

            return new FontDef
            {
                OutputName = "std.fnt",
                TexturePath = "common/data/font0.tim",
                DefaultLeft = 0,
                DefaultRight = 14,
                DefaultTop = -7,
                DefaultBottom = 7,
                CellSize = 14,
                CellCols = 18,
                Kerning = true,
                Characters = characters,
            };
        }

        /// <summary>
        /// The small font: 8px cells, 32 per row on font0.tim. There is no
        /// space character (missing glyphs draw nothing and advance by the
        /// default width). Cell 0 is an up arrow, then ASCII from '!' at
        /// cell 1 onwards. Metrics for A-Za-z0-9 are measured from the
        /// reference PNG when available so every glyph gets a uniform ink
        /// gap; other characters use a full-cell advance.
        /// </summary>
        private FontDef BuildSmallFont()
        {
            var characters = new List<Glyph>();
            void Add(uint codepoint, int cell)
            {
                var glyph = new Glyph { Codepoint = codepoint, Cell = cell };
                if (MetricOverrides.TryGetValue(codepoint, out var overridden))
                {
                    glyph.Left = overridden.Left;
                    glyph.Right = overridden.Right;
                }
                else if (TryMeasureMetrics(codepoint, cell, 8, 32, out var measured))
                {
                    glyph.Left = measured.Left;
                    glyph.Right = measured.Right;
                    Console.WriteLine(
                        "  '{0}' U+{1:X4}: left = {2}, right = {3}",
                        (char)codepoint,
                        codepoint,
                        measured.Left,
                        measured.Right);
                }
                else
                {
                    glyph.Left = 0;
                    glyph.Right = 8;
                }
                characters.Add(glyph);
            }

            Add(0x2191, 0); // up arrow
            for (uint cp = 0x21; cp <= 0x7F; cp++)
                Add(cp, (int)(cp - 0x20));

            return new FontDef
            {
                OutputName = "sm.fnt",
                TexturePath = "common/data/font0.tim",
                DefaultLeft = 0,
                DefaultRight = 8,
                DefaultTop = -4,
                DefaultBottom = 4,
                CellSize = 8,
                CellCols = 32,
                Kerning = false,
                Characters = characters,
            };
        }

        private static bool TryGetKerning(int cell, out int left, out int right)
        {
            var index = cell - 36;
            if (index >= 0 && index < WidthTbl.Length / 2)
            {
                left = WidthTbl[index * 2];
                right = WidthTbl[index * 2 + 1];
                return true;
            }
            left = 0;
            right = 0;
            return false;
        }

        /// <summary>
        /// Measures kerning metrics from the reference atlas image for
        /// A-Za-z0-9: left = transparent columns left of the ink (so the ink
        /// starts exactly on the pen), right = inkWidth + gap - left (so the
        /// gap to the next glyph's ink is exactly --gap pixels).
        /// </summary>
        private bool TryMeasureMetrics(uint codepoint, int cell, int cellSize, int cellCols, out (int Left, int Right) metrics)
        {
            metrics = default;
            if (_ink == null || !char.IsAsciiLetterOrDigit((char)codepoint))
                return false;

            var srcX = cellSize * (cell % cellCols);
            var srcY = cellSize * (cell / cellCols);

            int firstInk = -1;
            int lastInk = -1;
            for (var x = srcX; x < srcX + cellSize; x++)
            {
                var columnHasInk = false;
                for (var y = srcY; y < srcY + cellSize; y++)
                {
                    if (!_ink.Transparent[(y * _ink.Width) + x])
                    {
                        columnHasInk = true;
                        break;
                    }
                }
                if (columnHasInk)
                {
                    if (firstInk == -1)
                        firstInk = x - srcX;
                    lastInk = x - srcX;
                }
            }
            if (firstInk == -1)
                return false; // blank cell; fall back to table metrics

            var transparentLeft = firstInk;
            var transparentRight = cellSize - 1 - lastInk;
            var inkWidth = cellSize - transparentLeft - transparentRight;
            metrics.Left = transparentLeft;
            metrics.Right = inkWidth + _gap - transparentLeft;
            return true;
        }

        /// <summary>A per-pixel transparency mask of a decoded image.</summary>
        private sealed class InkMask
        {
            public int Width;
            public int Height;
            public bool[] Transparent = Array.Empty<bool>();
        }

        /// <summary>
        /// Minimal PNG reader that extracts a per-pixel transparency mask.
        /// Supports non-interlaced 8-bit images: paletted (with tRNS), grey+alpha
        /// and RGBA. Enough for our decoded-atlas reference images without
        /// pulling in an imaging dependency.
        /// </summary>
        private static InkMask LoadPngTransparency(string path)
        {
            var data = File.ReadAllBytes(path);
            if (data.Length < 8 || !data.AsSpan(0, 8).SequenceEqual(new byte[] { 0x89, (byte)'P', (byte)'N', (byte)'G', 0x0D, 0x0A, 0x1A, 0x0A }))
                throw new InvalidDataException($"{path}: not a PNG");

            uint width = 0;
            uint height = 0;
            byte bitDepth = 0;
            byte colorType = 0;
            byte interlace = 0;
            byte[]? palette = null;
            byte[]? trns = null;
            var idat = new List<byte[]>();
            for (var pos = 8; pos + 8 <= data.Length;)
            {
                var length = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(pos));
                var type = System.Text.Encoding.ASCII.GetString(data, pos + 4, 4);
                var chunk = data.AsSpan(pos + 8, (int)length);
                pos += 12 + (int)length; // length + type + data + crc
                if (type == "IHDR")
                {
                    width = BinaryPrimitives.ReadUInt32BigEndian(chunk);
                    height = BinaryPrimitives.ReadUInt32BigEndian(chunk.Slice(4));
                    bitDepth = chunk[8];
                    colorType = chunk[9];
                    interlace = chunk[12];
                }
                else if (type == "PLTE")
                {
                    palette = chunk.ToArray();
                }
                else if (type == "tRNS")
                {
                    trns = chunk.ToArray();
                }
                else if (type == "IDAT")
                {
                    idat.Add(chunk.ToArray());
                }
                else if (type == "IEND")
                {
                    break;
                }
            }

            if (bitDepth != 8 || interlace != 0)
                throw new InvalidDataException($"{path}: only 8-bit non-interlaced PNGs are supported");
            if (colorType != 3 && colorType != 4 && colorType != 6)
                throw new InvalidDataException($"{path}: colour type {colorType} has no alpha information");
            var channels = colorType switch
            {
                3 => 1,
                4 => 2,
                _ => 4,
            };

            // Inflate the concatenated IDAT payload (skip the 2-byte zlib header).
            using var compressed = new MemoryStream(System.Linq.Enumerable.Aggregate(idat, new List<byte>(), (acc, c) =>
            {
                acc.AddRange(c);
                return acc;
            }).ToArray(), 2, Math.Max(0, idat.Sum(c => c.Length) - 2));
            using var inflater = new DeflateStream(compressed, CompressionMode.Decompress);
            using var raw = new MemoryStream();
            inflater.CopyTo(raw);
            var bytes = raw.ToArray();

            // Unfilter scanlines into per-pixel bytes.
            var stride = (int)(width * channels);
            var pixels = new byte[height * width * channels];
            var prev = new byte[stride];
            var pos2 = 0;
            for (var y = 0; y < (int)height; y++)
            {
                if (pos2 + 1 + stride > bytes.Length)
                    throw new InvalidDataException($"{path}: truncated image data");
                var filter = bytes[pos2++];
                var line = bytes.AsSpan(pos2, stride).ToArray();
                pos2 += stride;
                for (var x = 0; x < stride; x++)
                {
                    var a = x >= channels ? line[x - channels] : (byte)0;
                    var b = prev[x];
                    var c = x >= channels ? prev[x - channels] : (byte)0;
                    switch (filter)
                    {
                        case 1:
                            line[x] += a;
                            break;
                        case 2:
                            line[x] += b;
                            break;
                        case 3:
                            line[x] += (byte)((a + b) / 2);
                            break;
                        case 4:
                        {
                            var p = a + b - c;
                            var pa = Math.Abs(p - a);
                            var pb = Math.Abs(p - b);
                            var pc = Math.Abs(p - c);
                            line[x] += (byte)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
                            break;
                        }
                    }
                }
                Buffer.BlockCopy(line, 0, pixels, y * stride, stride);
                prev = line;
            }

            // Transparency test per pixel.
            bool IsTransparent(int pixelIndex)
            {
                var o = pixelIndex * channels;
                return colorType switch
                {
                    // Paletted: the pixel stores a palette index; tRNS holds
                    // one alpha byte per palette entry.
                    3 => trns != null && pixels[o] < trns.Length && trns[pixels[o]] == 0,
                    4 => pixels[o + 1] == 0,
                    _ => pixels[o + 3] == 0,
                };
            }

            var mask = new bool[width * height];
            for (var i = 0; i < mask.Length; i++)
                mask[i] = IsTransparent(i);
            var transparentCount = mask.Count(static t => t);
            Console.WriteLine($"png {width}x{height}: {transparentCount}/{mask.Length} transparent px");
            return new InkMask { Width = (int)width, Height = (int)height, Transparent = mask };
        }

        /// <summary>
        /// Sanity-checks that the referenced TIM exists and is a 256x256
        /// paletted image, so bad output fails loudly at generation time.
        /// </summary>
        private bool ValidateTexture(string logicalPath)
        {
            var path = Path.Combine(_dataDir, logicalPath.Replace('/', '\\'));
            if (!File.Exists(path))
            {
                Console.Error.WriteLine($"texture not found: {path}");
                return false;
            }

            using var fs = File.OpenRead(path);
            Span<byte> buf = stackalloc byte[4];
            if (fs.Read(buf) != 4 || BinaryPrimitives.ReadUInt32LittleEndian(buf) != 0x10)
            {
                Console.Error.WriteLine($"not a TIM file: {path}");
                return false;
            }
            if (fs.Read(buf) != 4)
            {
                Console.Error.WriteLine($"truncated TIM header: {path}");
                return false;
            }
            var flags = BinaryPrimitives.ReadUInt32LittleEndian(buf);
            if ((flags & 0x8) != 0)
            {
                // Skip the whole CLUT block (its size field counts itself).
                if (fs.Read(buf) != 4)
                {
                    Console.Error.WriteLine($"truncated CLUT block: {path}");
                    return false;
                }
                var clutSize = BinaryPrimitives.ReadUInt32LittleEndian(buf);
                fs.Seek(clutSize - 4, SeekOrigin.Current);
            }
            if ((flags & 0x7) > 1) // only 4bpp / 8bpp supported by our fonts
            {
                Console.Error.WriteLine($"unexpected TIM bpp flags {flags:X}: {path}");
                return false;
            }

            Span<byte> img = stackalloc byte[12];
            if (fs.Read(img) != 12)
            {
                Console.Error.WriteLine($"truncated image header: {path}");
                return false;
            }
            var orgW = BinaryPrimitives.ReadUInt16LittleEndian(img.Slice(8));
            var orgH = BinaryPrimitives.ReadUInt16LittleEndian(img.Slice(10));
            var bpp = (flags & 0x7) == 0 ? 4 : 8;
            var width = orgW * (bpp == 4 ? 4 : 2);
            if (width != 256 || orgH != 256)
            {
                Console.Error.WriteLine($"unexpected TIM size {width}x{orgH}: {path}");
                return false;
            }
            return true;
        }

        private static void WriteFont(string path, FontDef font)
        {
            // Layout: header, texture directory, character records, then the
            // UTF-8 path strings blob referenced by absolute byte offsets.
            var textures = new List<string> { font.TexturePath };

            var stringsOffset = HeaderSize + textures.Count * TextureEntrySize + font.Characters.Count * CharRecordSize;
            var stringOffsets = new List<long>();
            long offset = stringsOffset;
            foreach (var texture in textures)
            {
                stringOffsets.Add(offset);
                offset += Encoding.UTF8.GetByteCount(texture) + 1;
            }

            using var fs = File.Create(path);
            WriteHeader(fs, (uint)textures.Count, (uint)font.Characters.Count, font);
            Span<byte> entry = stackalloc byte[TextureEntrySize];
            for (var i = 0; i < textures.Count; i++)
            {
                BinaryPrimitives.WriteUInt32LittleEndian(entry, 256);
                BinaryPrimitives.WriteUInt32LittleEndian(entry.Slice(4), 256);
                BinaryPrimitives.WriteUInt64LittleEndian(entry.Slice(8), (ulong)stringOffsets[i]);
                fs.Write(entry);
            }
            Span<byte> record = stackalloc byte[CharRecordSize];
            foreach (var glyph in font.Characters)
            {
                var srcX = (uint)(font.CellSize * (glyph.Cell % font.CellCols));
                var srcY = (uint)(font.CellSize * (glyph.Cell / font.CellCols));
                BinaryPrimitives.WriteUInt32LittleEndian(record, glyph.Codepoint);
                BinaryPrimitives.WriteUInt16LittleEndian(record.Slice(4), 0);
                BinaryPrimitives.WriteUInt16LittleEndian(record.Slice(6), 0);
                BinaryPrimitives.WriteInt32LittleEndian(record.Slice(8), glyph.Left);
                BinaryPrimitives.WriteInt32LittleEndian(record.Slice(12), 0); // top (vertical bearing, unused)
                BinaryPrimitives.WriteInt32LittleEndian(record.Slice(16), glyph.Right);
                BinaryPrimitives.WriteInt32LittleEndian(record.Slice(20), 0); // bottom (vertical bearing, unused)
                BinaryPrimitives.WriteUInt32LittleEndian(record.Slice(24), srcX);
                BinaryPrimitives.WriteUInt32LittleEndian(record.Slice(28), srcY);
                BinaryPrimitives.WriteUInt32LittleEndian(record.Slice(32), (uint)font.CellSize);
                BinaryPrimitives.WriteUInt32LittleEndian(record.Slice(36), (uint)font.CellSize);
                fs.Write(record);
            }
            foreach (var texture in textures)
            {
                var bytes = new byte[Encoding.UTF8.GetByteCount(texture) + 1];
                Encoding.UTF8.GetBytes(texture, bytes);
                fs.Write(bytes);
            }
        }

        private static void WriteHeader(Stream stream, uint numTextures, uint numCharacters, FontDef font)
        {
            Span<byte> header = stackalloc byte[HeaderSize];
            BinaryPrimitives.WriteUInt32LittleEndian(header, Magic);
            BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(4), numTextures);
            BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(8), numCharacters);
            BinaryPrimitives.WriteInt32LittleEndian(header.Slice(12), (int)font.DefaultLeft);
            BinaryPrimitives.WriteInt32LittleEndian(header.Slice(16), (int)font.DefaultRight);
            BinaryPrimitives.WriteInt32LittleEndian(header.Slice(20), font.DefaultTop);
            BinaryPrimitives.WriteInt32LittleEndian(header.Slice(24), font.DefaultBottom);
            BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(28), 0);
            stream.Write(header);
        }
    }
}
