using System.Text;

namespace interopgen
{
    internal class Program
    {
        static int Main(string[] args) => new Program().Go(args);

        private readonly List<InteropStruct> _structs = new List<InteropStruct>();
        private readonly Dictionary<string, string> _typedefs = new Dictionary<string, string>();
        private InteropStruct? _currentStruct;

        public int Go(string[] args)
        {
            if (args.Length == 0)
            {
                Console.Error.WriteLine("usage: <input>");
                return 1;
            }

            var inputPath = args[0];
            var input = File.ReadAllLines(inputPath);
            for (int i = 0; i < input.Length; i++)
            {
                string? line = input[i];
                try
                {
                    ProcessLine(line);
                }
                catch
                {
                    Console.Error.WriteLine($"Failed to process line {i}");
                    return 2;
                }
            }
            ProcessStructs();
            return 0;
        }

        private void ProcessLine(string line)
        {
            line = line.Trim();
            var commentStart = line.IndexOf('#');
            if (commentStart != -1)
            {
                line = line[..commentStart];
            }

            var lineArgs = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (lineArgs.Length == 0)
                return;

            switch (lineArgs[0])
            {
                case "typedef":
                    _typedefs.Add(lineArgs[1], lineArgs[2]);
                    break;
                case "struct":
                    if (lineArgs.Length > 2)
                    {
                        _currentStruct = new InteropStruct(lineArgs[1], ParseInt(lineArgs[2]));
                    }
                    else
                    {
                        _currentStruct = new InteropStruct(lineArgs[1]);
                    }
                    _structs.Add(_currentStruct);
                    break;
                case "base":
                    _currentStruct!.Base = lineArgs[1];
                    break;
                case "declare":
                    {
                        var typeSyntax = lineArgs[2];
                        var typeName = typeSyntax;
                        var arrayLen = null as int?;
                        var lBracket = typeSyntax.IndexOf('[');
                        if (lBracket != -1)
                        {
                            var rBracket = typeSyntax.IndexOf("]");
                            typeName = typeSyntax[..lBracket];
                            arrayLen = ParseInt(typeSyntax.Substring(lBracket + 1, rBracket - lBracket - 1));
                        }
                        var address = null as int?;
                        if (lineArgs.Length > 3)
                        {
                            address = ParseInt(lineArgs[3]);
                        }
                        _currentStruct!.Members.Add(new InteropMember(lineArgs[1], typeName, arrayLen, address));
                        break;
                    }
            }
        }

        private void ProcessStructs()
        {
            foreach (var s in _structs)
            {
                ProcessStruct(s);
            }
            var header = GenerateHeader();
            Console.WriteLine(header);
        }

        private string GenerateHeader()
        {
            var sb = new StringBuilder();
            sb.AppendLine("#pragma once");
            sb.AppendLine();
            sb.AppendLine("#include <cstdint>");
            sb.AppendLine("#include <cstddef>");
            sb.AppendLine();
            sb.AppendLine("#pragma pack(push, 1)");
            sb.AppendLine();

            foreach (var kvp in _typedefs)
            {
                sb.AppendLine($"using {kvp.Key} = {GetRealTypeName(kvp.Value)};");
            }
            sb.AppendLine();
            foreach (var s in _structs)
            {
                if (s.Base == null)
                {
                    sb.AppendLine($"struct {s.Name}");
                }
                else
                {
                    sb.AppendLine($"struct {s.Name} : {s.Base}");
                }
                sb.AppendLine("{");
                foreach (var m in s.Members)
                {
                    var sb2 = new StringBuilder();
                    sb2.Append("    ");
                    sb2.Append(GetRealTypeName(m.Type));
                    sb2.Append(' ');
                    sb2.Append(m.Name);
                    if (m.ArrayLength != null)
                    {
                        sb2.Append('[');
                        sb2.Append(m.ArrayLength);
                        sb2.Append(']');
                    }
                    sb2.Append(';');

                    while (sb2.Length < 40)
                        sb2.Append(' ');
                    sb2.AppendFormat("// 0x{0:X4}", m.Address);
                    sb.AppendLine(sb2.ToString());
                }
                sb.AppendLine("};");
                sb.AppendLine($"static_assert(sizeof({s.Name}) == 0x{s.Size:X2});");
                sb.AppendLine();
            }

            sb.AppendLine("#pragma pack(pop)");
            return sb.ToString();
        }

        private static string GetRealTypeName(string name)
        {
            var asterisk = name.IndexOf('*');
            if (asterisk != -1)
            {
                return GetRealTypeName(name.Substring(0, asterisk)) + name.Substring(asterisk);
            }
            return name switch
            {
                "void" => "void",
                "s8" => "int8_t",
                "u8" => "uint8_t",
                "s16" => "int16_t",
                "u16" => "uint16_t",
                "s32" => "int32_t",
                "u32" => "uint32_t",
                _ => name,
            };
        }

        private InteropStruct ProcessStruct(string name)
        {
            var s = _structs.First(x => x.Name == name);
            return ProcessStruct(s);
        }

        private InteropStruct ExpectProcessedStruct(string name)
        {
            var s = _structs.FirstOrDefault(x => x.Name == name);
            if (s == null || !s.Processed)
                throw new Exception($"{name} not defined beforehand");
            return s;
        }

        private InteropStruct ProcessStruct(InteropStruct s)
        {
            if (s.Processed)
                return s;
            if (s.Processing)
                throw new Exception();

            s.Processing = true;

            var address = 0;
            var baseSize = 0;
            if (s.Base != null)
            {
                address = ExpectProcessedStruct(s.Base).Size!.Value;
                baseSize = address;
            }

            foreach (var member in s.Members)
            {
                member.Size = GetSize(member.Type) * (member.ArrayLength ?? 1);
                if (member.Address == null)
                {
                    member.Address = address;
                    address += member.Size.Value;
                }
                else
                {
                    address = member.Address.Value + member.Size.Value;
                }
            }

            if (s.Size == null)
            {
                s.Size = baseSize + (address - s.Members[0].Address);
            }

            // Add paddings
            for (var i = 1; i < s.Members.Count; i++)
            {
                var prev = s.Members[i - 1];
                var curr = s.Members[i];
                var padAddress = prev.Address + prev.Size!.Value;
                var padLength = curr.Address - padAddress;
                if (curr.Address > padAddress)
                {
                    var padMember = new InteropMember($"pad_{padAddress:X4}", "u8", padLength, padAddress);
                    padMember.Size = padLength;
                    s.Members.Insert(i, padMember);
                }
            }

            s.Processed = true;
            return s;
        }

        private int GetSize(string type)
        {
            if (_typedefs.TryGetValue(type, out var alias))
            {
                return GetSize(alias);
            }
            if (type.EndsWith("*"))
            {
                // GetSize(type.TrimEnd('*'));
                return 4;
            }
            return type switch
            {
                "void" => 0,
                "u8" or "s8" => 1,
                "u16" or "s16" => 2,
                "u32" or "s32" => 4,
                _ => ExpectProcessedStruct(type).Size!.Value,
            };
        }

        private static int ParseInt(string value)
        {
            if (value.StartsWith("0x"))
            {
                return int.Parse(value[2..], System.Globalization.NumberStyles.HexNumber);
            }
            return int.Parse(value);
        }
    }

    internal class InteropStruct(string name, int? size = null)
    {
        public string Name { get; set; } = name;
        public int? Size { get; set; } = size;
        public string? Base { get; set; }
        public List<InteropMember> Members { get; } = new List<InteropMember>();
        public bool Processing { get; set; }
        public bool Processed { get; set; }
    }

    internal class InteropMember(string name, string type, int? arrayLength, int? address = null)
    {
        public string Name { get; set; } = name;
        public string Type { get; set; } = type;
        public int? ArrayLength { get; set; } = arrayLength;
        public int? Address { get; set; } = address;
        public int? Size { get; set; }
    }
}
