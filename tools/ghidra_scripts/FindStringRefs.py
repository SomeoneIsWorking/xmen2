#@runtime Jython
"""Find a string and walk its incoming data/code reference chain."""
import os
import jarray

from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

query = os.environ.get("FIND_STRING_REFS", "")
if not query:
    raise RuntimeError("FIND_STRING_REFS is required")

max_depth = int(os.environ.get("FIND_STRING_DEPTH", "4"))
backtrack = int(os.environ.get("FIND_STRING_BACKTRACK", "0"))
decompile_code = os.environ.get("FIND_STRING_DECOMPILE", "0") == "1"
if backtrack < 0 or backtrack % 4:
    raise RuntimeError("FIND_STRING_BACKTRACK must be a non-negative multiple of 4")
listing = currentProgram.getListing()
functions = currentProgram.getFunctionManager()
references = currentProgram.getReferenceManager()
memory = currentProgram.getMemory()

matches = []
for data in listing.getDefinedData(True):
    value = data.getValue()
    if value is not None and query.lower() in str(value).lower():
        matches.append(data.getAddress())

if not matches:
    raise RuntimeError("no defined string contains %r" % query)

decompiler = None
if decompile_code:
    decompiler = DecompInterface()
    decompiler.setOptions(DecompileOptions())
    decompiler.toggleCCode(True)
    decompiler.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

seen_addresses = set()
seen_functions = set()
frontier = []
for address in matches:
    # MSVC TypeDescriptor names begin eight bytes into the descriptor. Seed
    # both forms so RTTI users are visible even when Ghidra references the
    # descriptor rather than its embedded name.
    frontier.append((address, 0))
    frontier.append((address.subtract(8), 0))

print("STRING %r: %s" % (query, ", ".join(str(a) for a in matches)))
while frontier:
    address, depth = frontier.pop(0)
    key = address.getOffset()
    if key in seen_addresses or depth > max_depth:
        continue
    seen_addresses.add(key)

    sources = [(ref.getFromAddress(), str(ref.getReferenceType()))
               for ref in references.getReferencesTo(address)]

    # Ghidra does not materialize every MSVC RTTI/data-pointer reference. Scan
    # aligned initialized data as a second instrument; code immediates remain
    # covered by the reference database and need not be byte-pattern guessed.
    wanted = address.getOffset() & 0xffffffff
    known = set(source.getOffset() for source, _kind in sources)
    for block in memory.getBlocks():
        if not block.isInitialized() or block.isExecute():
            continue
        size = int(block.getSize())
        raw = jarray.zeros(size, "b")
        memory.getBytes(block.getStart(), raw)
        for offset in range(0, size - 3, 4):
            value = ((raw[offset] & 0xff) |
                     ((raw[offset + 1] & 0xff) << 8) |
                     ((raw[offset + 2] & 0xff) << 16) |
                     ((raw[offset + 3] & 0xff) << 24))
            if value == wanted:
                source = block.getStart().add(offset)
                if source.getOffset() not in known:
                    known.add(source.getOffset())
                    sources.append((source, "RAW_ALIGNED_POINTER"))

    print("DEPTH %d %s: %d incoming reference(s)" %
          (depth, address, len(sources)))
    for source, kind in sources:
        function = functions.getFunctionContaining(source)
        if function:
            entry = function.getEntryPoint().getOffset()
            print("  CODE %s in %s @ %s" %
                  (source, function.getName(), function.getEntryPoint()))
            if decompile_code and entry not in seen_functions:
                seen_functions.add(entry)
                result = decompiler.decompileFunction(function, 60, monitor)
                if result.decompileCompleted():
                    print(result.getDecompiledFunction().getC())
                else:
                    print("DECOMPILE FAILED: %s" % result.getErrorMessage())
        else:
            print("  DATA %s (%s)" % (source, kind))
            # A pointer can be a field inside an RTTI or registry structure.
            # Optional bounded backtracking lets the caller test candidate
            # structure starts without hard-coding one compiler layout here.
            for delta in range(0, backtrack + 1, 4):
                candidate = source.subtract(delta)
                if memory.contains(candidate):
                    frontier.append((candidate, depth + 1))
