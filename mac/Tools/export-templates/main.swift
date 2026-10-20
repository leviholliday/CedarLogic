// export-templates <cl_gatedefs.xml> <out dir>: writes every built-in template
// of the Mac app as template-<id>.cdl (and -running.cdl, -pulse.cdl for the
// Clock choices) plus index.tsv: id, group, name, detail, choices.
import Foundation

let args = CommandLine.arguments
guard args.count == 3, cl_library_load(args[1]) else {
    FileHandle.standardError.write("usage: export-templates <cl_gatedefs.xml> <out dir>\n".data(using: .utf8)!)
    exit(2)
}
let dir = URL(fileURLWithPath: args[2], isDirectory: true)
try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
var index = ""
MainActor.assumeIsolated {
    for t in Templates.builtIn {
        try! t.text.write(to: dir.appendingPathComponent("template-\(t.id).cdl"), atomically: true, encoding: .utf8)
        var choices = ""
        if let r = t.runningText {
            try! r.write(to: dir.appendingPathComponent("template-\(t.id)-running.cdl"), atomically: true, encoding: .utf8)
            choices += "running"
        }
        if let p = t.pulseText {
            try! p.write(to: dir.appendingPathComponent("template-\(t.id)-pulse.cdl"), atomically: true, encoding: .utf8)
            choices += choices.isEmpty ? "pulse" : ",pulse"
        }
        index += [t.id, t.group, t.name, t.detail, choices].joined(separator: "\t") + "\n"
    }
}
try index.write(to: dir.appendingPathComponent("index.tsv"), atomically: true, encoding: .utf8)
print("wrote", index.split(separator: "\n").count, "templates to", dir.path)
