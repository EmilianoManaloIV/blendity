# SPDX-License-Identifier: GPL-2.0-or-later
# Live Link's framing (docs/LIVELINK.md): a line of JSON, and when it ends in "bin": N, N bytes after its newline.
extends RefCounted

var _buf := PackedByteArray()
var _pos := 0


func feed(data: PackedByteArray) -> void:
	if _pos > 0 and _pos >= _buf.size() / 2:  # (what was read goes, now and then: no copy per message)
		_buf = _buf.slice(_pos)
		_pos = 0
	_buf.append_array(data)


func pending_bytes() -> int:
	return _buf.size() - _pos


## The next whole message as [Dictionary, PackedByteArray], or null until more bytes come.
func next_message():
	var nl := _buf.find(10, _pos)
	if nl < 0:
		return null
	var line := _buf.slice(_pos, nl).get_string_from_utf8()
	var j = JSON.parse_string(line)
	if typeof(j) != TYPE_DICTIONARY:
		_pos = nl + 1  # (a line that isn't an object: skipped, as unknown messages are)
		return [{}, PackedByteArray()]
	var n := int(j.get("bin", 0))
	if _buf.size() < nl + 1 + n:
		return null
	var att := _buf.slice(nl + 1, nl + 1 + n) if n > 0 else PackedByteArray()
	_pos = nl + 1 + n
	return [j, att]
