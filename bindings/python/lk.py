"""
lk -- Python binding for the lk UI toolkit (ctypes, proof-of-concept).

Loads liblk.so and wraps the public accessor API in Pythonic classes.
No external dependencies beyond the Python standard library.

Usage:
    from lk import UI, Kind, Prop, Event
    ui = UI("path/to/liblk.so")
    tree = ui.begin_frame()
    ...
"""

import ctypes
import os
from ctypes import (
    c_int, c_uint, c_ushort, c_ubyte, c_char_p, c_void_p,
    Structure, Union, POINTER, CFUNCTYPE, byref,
)

# ---------------------------------------------------------------------------
# C type aliases
# ---------------------------------------------------------------------------

lk_u8 = c_ubyte
lk_u16 = c_ushort
lk_u32 = c_uint
lk_i32 = c_int
lk_ix = lk_u32
lk_node_id = lk_u32


# ---------------------------------------------------------------------------
# Struct / union declarations (must match lk-data.h exactly)
# ---------------------------------------------------------------------------

class _val_as(Union):
    _fields_ = [("b", lk_u8), ("i", lk_u32), ("str_id", lk_u32)]


class lk_value(Structure):
    _fields_ = [("tag", c_int), ("as_", _val_as)]


class lk_change(Structure):
    _fields_ = [("kind", lk_u8), ("id", lk_node_id), ("node_ix", lk_ix)]


class lk_changeset(Structure):
    _fields_ = [
        ("changes", POINTER(lk_change)),
        ("count", lk_u32),
        ("cap", lk_u32),
    ]


class lk_command(Structure):
    _fields_ = [
        ("name", lk_u32),
        ("args", lk_value * 4),
        ("arg_count", lk_u8),
        ("source_node", lk_ix),
        ("source_ptype", lk_u32),
    ]


class lk_command_queue(Structure):
    _fields_ = [
        ("cmds", POINTER(lk_command)),
        ("count", lk_u32),
        ("cap", lk_u32),
    ]


class lk_rect(Structure):
    _fields_ = [("x", lk_i32), ("y", lk_i32), ("w", lk_i32), ("h", lk_i32)]


class _ev_pointer(Structure):
    _fields_ = [("x", lk_i32), ("y", lk_i32), ("button", lk_u8)]


class _ev_key(Structure):
    _fields_ = [("keycode", lk_u16), ("repeat", lk_u8)]


class _ev_text(Structure):
    _fields_ = [("buf", c_ubyte * 32), ("len", lk_u8)]


class _ev_wheel(Structure):
    _fields_ = [("dx", lk_i32), ("dy", lk_i32)]


class _ev_window(Structure):
    _fields_ = [("w", lk_i32), ("h", lk_i32)]


class _ev_data(Union):
    _fields_ = [
        ("pointer", _ev_pointer),
        ("key", _ev_key),
        ("text", _ev_text),
        ("wheel", _ev_wheel),
        ("window", _ev_window),
    ]


class lk_event(Structure):
    _fields_ = [
        ("type", lk_u8),
        ("phase", lk_u8),
        ("mods", lk_u8),
        ("handled", lk_u8),
        ("target", lk_ix),
        ("data", _ev_data),
    ]


# Callback types
LK_EVENT_HANDLER = CFUNCTYPE(c_int, POINTER(lk_event), lk_ix, c_void_p)
LK_COMMAND_HANDLER = CFUNCTYPE(None, POINTER(lk_command), c_void_p)


# ---------------------------------------------------------------------------
# Enum constants
# ---------------------------------------------------------------------------

class Kind:
    WINDOW = 1
    ROW = 2
    COLUMN = 3
    SPACER = 4
    LABEL = 5
    BUTTON = 6

    _NAMES = {1: "window", 2: "row", 3: "column",
              4: "spacer", 5: "label", 6: "button"}

    @classmethod
    def name(cls, k):
        return cls._NAMES.get(k, f"kind({k})")


class Prop:
    TEXT = 1
    FOCUSABLE = 2
    DISABLED = 3
    W = 4
    H = 5
    PADDING = 6
    GAP = 7
    ALIGN = 8
    JUSTIFY = 9


class ValueTag:
    NONE = 0
    BOOL = 1
    I32 = 2
    STR = 3


class Event:
    NONE = 0
    POINTER_MOVE = 1
    POINTER_DOWN = 2
    POINTER_UP = 3
    KEY_DOWN = 4
    KEY_UP = 5
    TEXT = 6
    WHEEL = 7
    WINDOW_RESIZE = 8
    WINDOW_CLOSE = 9


class Phase:
    CAPTURE = 1
    TARGET = 2
    BUBBLE = 3


class Key:
    UNKNOWN = 0
    TAB = 1
    RETURN = 2
    ESCAPE = 3
    BACKSPACE = 4
    DELETE = 5
    SPACE = 6
    LEFT = 7
    RIGHT = 8
    UP = 9
    DOWN = 10
    HOME = 11
    END = 12


class ChangeKind:
    ADDED = 1
    REMOVED = 2
    UPDATED = 3

    _NAMES = {1: "ADDED", 2: "REMOVED", 3: "UPDATED"}

    @classmethod
    def name(cls, k):
        return cls._NAMES.get(k, f"?({k})")


# ---------------------------------------------------------------------------
# Library loader + function signatures
# ---------------------------------------------------------------------------

def _load_lib(path):
    lib = ctypes.CDLL(path)

    # -- intern (binding-friendly const char* API) --
    lib.lk_intern_cid.argtypes = [c_void_p, c_char_p]
    lib.lk_intern_cid.restype = lk_node_id
    lib.lk_intern_cstr.argtypes = [c_void_p, lk_node_id]
    lib.lk_intern_cstr.restype = c_char_p

    # -- value constructors (still needed for add_prop / add_presentation) --
    lib.lk_v_bool.argtypes = [c_int]
    lib.lk_v_bool.restype = lk_value
    lib.lk_v_i32.argtypes = [lk_i32]
    lib.lk_v_i32.restype = lk_value
    lib.lk_v_cstr.argtypes = [c_void_p, c_char_p]
    lib.lk_v_cstr.restype = lk_value

    # -- UI lifecycle --
    lib.lk_ui_create.argtypes = [c_void_p]
    lib.lk_ui_create.restype = c_void_p
    lib.lk_ui_destroy.argtypes = [c_void_p]
    lib.lk_ui_destroy.restype = None
    lib.lk_ui_begin_frame.argtypes = [c_void_p]
    lib.lk_ui_begin_frame.restype = c_void_p
    lib.lk_ui_end_frame.argtypes = [c_void_p]
    lib.lk_ui_end_frame.restype = POINTER(lk_changeset)
    lib.lk_ui_tree.argtypes = [c_void_p]
    lib.lk_ui_tree.restype = c_void_p
    lib.lk_ui_intern.argtypes = [c_void_p]
    lib.lk_ui_intern.restype = c_void_p

    # -- tree building (binding-friendly const char* API) --
    lib.lk_tree_add_node_c.argtypes = [c_void_p, c_char_p, c_int]
    lib.lk_tree_add_node_c.restype = lk_ix
    lib.lk_tree_set_root.argtypes = [c_void_p, lk_ix]
    lib.lk_tree_set_root.restype = None
    lib.lk_tree_append_child.argtypes = [c_void_p, lk_ix, lk_ix]
    lib.lk_tree_append_child.restype = None
    lib.lk_tree_add_prop.argtypes = [c_void_p, lk_ix, c_int, lk_value]
    lib.lk_tree_add_prop.restype = None
    lib.lk_tree_add_presentation_s.argtypes = [
        c_void_p, lk_ix, c_char_p, lk_value]
    lib.lk_tree_add_presentation_s.restype = None
    lib.lk_tree_find_by_id.argtypes = [c_void_p, lk_node_id]
    lib.lk_tree_find_by_id.restype = lk_ix

    # -- node accessors --
    lib.lk_node_id_get.argtypes = [c_void_p, lk_ix]
    lib.lk_node_id_get.restype = lk_node_id
    lib.lk_node_kind_get.argtypes = [c_void_p, lk_ix]
    lib.lk_node_kind_get.restype = lk_u16
    lib.lk_node_parent.argtypes = [c_void_p, lk_ix]
    lib.lk_node_parent.restype = lk_ix
    lib.lk_node_first_child.argtypes = [c_void_p, lk_ix]
    lib.lk_node_first_child.restype = lk_ix
    lib.lk_node_next_sibling.argtypes = [c_void_p, lk_ix]
    lib.lk_node_next_sibling.restype = lk_ix

    # -- node text (binding-friendly const char* API) --
    lib.lk_node_text_cstr.argtypes = [c_void_p, lk_ix]
    lib.lk_node_text_cstr.restype = c_char_p

    # -- tree accessors --
    lib.lk_tree_node_count.argtypes = [c_void_p]
    lib.lk_tree_node_count.restype = lk_u32
    lib.lk_tree_root.argtypes = [c_void_p]
    lib.lk_tree_root.restype = lk_ix
    lib.lk_tree_intern.argtypes = [c_void_p]
    lib.lk_tree_intern.restype = c_void_p

    # -- changeset accessors --
    lib.lk_changeset_count.argtypes = [POINTER(lk_changeset)]
    lib.lk_changeset_count.restype = lk_u32
    lib.lk_changeset_get.argtypes = [POINTER(lk_changeset), lk_u32]
    lib.lk_changeset_get.restype = POINTER(lk_change)

    # -- prop helpers --
    lib.lk_node_prop_i32.argtypes = [c_void_p, lk_ix, c_int, lk_i32]
    lib.lk_node_prop_i32.restype = lk_i32
    lib.lk_node_has_prop.argtypes = [c_void_p, lk_ix, c_int]
    lib.lk_node_has_prop.restype = c_int
    lib.lk_node_prop_bool.argtypes = [c_void_p, lk_ix, c_int]
    lib.lk_node_prop_bool.restype = c_int

    # -- layout --
    lib.lk_layout_simple.argtypes = [c_void_p, lk_i32, lk_i32,
                                     POINTER(lk_rect)]
    lib.lk_layout_simple.restype = c_int

    # -- hit test --
    lib.lk_hit_test.argtypes = [c_void_p, POINTER(lk_rect), lk_i32, lk_i32]
    lib.lk_hit_test.restype = lk_ix

    # -- event init helpers --
    lib.lk_event_init_pointer.argtypes = [POINTER(lk_event), lk_u8,
                                           lk_i32, lk_i32, lk_u8]
    lib.lk_event_init_pointer.restype = None
    lib.lk_event_init_key.argtypes = [POINTER(lk_event), lk_u8,
                                       lk_u16, lk_u8]
    lib.lk_event_init_key.restype = None

    # -- event routing --
    lib.lk_event_route.argtypes = [c_void_p, POINTER(lk_event)]
    lib.lk_event_route.restype = None
    lib.lk_ui_set_event_handler.argtypes = [c_void_p, LK_EVENT_HANDLER,
                                             c_void_p]
    lib.lk_ui_set_event_handler.restype = None

    # -- focus --
    lib.lk_focus_set.argtypes = [c_void_p, c_void_p, lk_node_id]
    lib.lk_focus_set.restype = c_int
    lib.lk_focus_clear.argtypes = [c_void_p]
    lib.lk_focus_clear.restype = None
    lib.lk_focus_next.argtypes = [c_void_p, c_void_p]
    lib.lk_focus_next.restype = lk_node_id
    lib.lk_focus_prev.argtypes = [c_void_p, c_void_p]
    lib.lk_focus_prev.restype = lk_node_id
    lib.lk_focus_current.argtypes = [c_void_p, c_void_p]
    lib.lk_focus_current.restype = lk_ix

    # -- command / translator --
    lib.lk_ui_add_translator_s.argtypes = [c_void_p, lk_u8, c_char_p,
                                            lk_u16, c_char_p]
    lib.lk_ui_add_translator_s.restype = None
    lib.lk_ui_commands.argtypes = [c_void_p]
    lib.lk_ui_commands.restype = POINTER(lk_command_queue)
    lib.lk_ui_clear_commands.argtypes = [c_void_p]
    lib.lk_ui_clear_commands.restype = None
    lib.lk_ui_set_command_handler.argtypes = [c_void_p, LK_COMMAND_HANDLER,
                                               c_void_p]
    lib.lk_ui_set_command_handler.restype = None

    # -- command accessors (binding-friendly typed API) --
    lib.lk_command_queue_count.argtypes = [POINTER(lk_command_queue)]
    lib.lk_command_queue_count.restype = lk_u32
    lib.lk_command_queue_get.argtypes = [POINTER(lk_command_queue), lk_u32]
    lib.lk_command_queue_get.restype = POINTER(lk_command)
    lib.lk_command_name.argtypes = [POINTER(lk_command)]
    lib.lk_command_name.restype = lk_u32
    lib.lk_command_arg_count.argtypes = [POINTER(lk_command)]
    lib.lk_command_arg_count.restype = lk_u8
    lib.lk_command_arg_tag.argtypes = [POINTER(lk_command), lk_u8]
    lib.lk_command_arg_tag.restype = lk_u8
    lib.lk_command_arg_i32.argtypes = [POINTER(lk_command), lk_u8]
    lib.lk_command_arg_i32.restype = lk_i32
    lib.lk_command_arg_str_id.argtypes = [POINTER(lk_command), lk_u8]
    lib.lk_command_arg_str_id.restype = lk_u32
    lib.lk_command_source_node.argtypes = [POINTER(lk_command)]
    lib.lk_command_source_node.restype = lk_ix
    lib.lk_command_source_ptype.argtypes = [POINTER(lk_command)]
    lib.lk_command_source_ptype.restype = lk_u32

    return lib


# ---------------------------------------------------------------------------
# Pythonic wrappers
# ---------------------------------------------------------------------------

def _intern_to_py(lib, intern, nid):
    """Look up an interned ID and return a Python string."""
    if nid == 0 or not intern:
        return ""
    raw = lib.lk_intern_cstr(intern, nid)
    if not raw:
        return ""
    return raw.decode("utf-8", errors="replace")


class Change:
    """A single entry in a changeset."""

    def __init__(self, kind, node_id_str, node_ix):
        self.kind = kind
        self.node_id_str = node_id_str
        self.node_ix = node_ix

    def __repr__(self):
        return (f"{ChangeKind.name(self.kind):>8s}  {self.node_id_str}"
                f"  (ix={self.node_ix})")


class Command:
    """A command emitted by a translator."""

    def __init__(self, name_str, args, source_node, source_ptype_str):
        self.name = name_str
        self.args = args
        self.source_node = source_node
        self.source_ptype = source_ptype_str

    def arg(self, idx):
        """Return the i-th argument's Python value (int, bool, str, or None)."""
        if idx >= len(self.args):
            return None
        tag, val = self.args[idx]
        if tag == ValueTag.NONE:
            return None
        if tag == ValueTag.BOOL:
            return bool(val)
        if tag == ValueTag.I32:
            return val
        if tag == ValueTag.STR:
            return val  # already resolved to string
        return val

    def __repr__(self):
        a = ", ".join(str(self.arg(i)) for i in range(len(self.args)))
        return f"{self.name}({a})  from={self.source_ptype}"


class Tree:
    """Wrapper around an lk_tree pointer (building or read-only)."""

    def __init__(self, lib, ptr, intern=None):
        self._lib = lib
        self._ptr = ptr
        self._intern = intern or lib.lk_tree_intern(ptr)

    @property
    def node_count(self):
        return self._lib.lk_tree_node_count(self._ptr)

    @property
    def root(self):
        return self._lib.lk_tree_root(self._ptr)

    def add_node(self, name, kind):
        return self._lib.lk_tree_add_node_c(
            self._ptr, name.encode("utf-8"), kind)

    def set_root(self, ix):
        self._lib.lk_tree_set_root(self._ptr, ix)

    def append_child(self, parent, child):
        self._lib.lk_tree_append_child(self._ptr, parent, child)

    def add_prop(self, node, key, value):
        """Add a property. value can be int, bool, or str."""
        if isinstance(value, bool):
            v = self._lib.lk_v_bool(1 if value else 0)
        elif isinstance(value, int):
            v = self._lib.lk_v_i32(value)
        elif isinstance(value, str):
            v = self._lib.lk_v_cstr(self._intern, value.encode("utf-8"))
        else:
            raise TypeError(f"unsupported prop type: {type(value)}")
        self._lib.lk_tree_add_prop(self._ptr, node, key, v)

    def set_text(self, node, text):
        self.add_prop(node, Prop.TEXT, text)

    def add_presentation(self, node, ptype, pvalue):
        """Attach a presentation. pvalue must be an int (i32)."""
        v = self._lib.lk_v_i32(pvalue)
        self._lib.lk_tree_add_presentation_s(
            self._ptr, node, ptype.encode("utf-8"), v)

    def find_by_id(self, name):
        nid = self._lib.lk_intern_cid(
            self._intern, name.encode("utf-8"))
        return self._lib.lk_tree_find_by_id(self._ptr, nid)

    def node_id_str(self, ix):
        nid = self._lib.lk_node_id_get(self._ptr, ix)
        return _intern_to_py(self._lib, self._intern, nid)

    def node_kind(self, ix):
        return self._lib.lk_node_kind_get(self._ptr, ix)

    def node_text(self, ix):
        raw = self._lib.lk_node_text_cstr(self._ptr, ix)
        if not raw:
            return ""
        return raw.decode("utf-8", errors="replace")

    def layout(self, viewport_w, viewport_h):
        """Run layout with stub text measurer. Returns list of lk_rect."""
        count = self.node_count
        rects = (lk_rect * count)()
        ok = self._lib.lk_layout_simple(self._ptr, viewport_w, viewport_h,
                                         rects)
        if not ok:
            raise RuntimeError("lk_layout_simple failed")
        return rects

    def hit_test(self, rects, x, y):
        return self._lib.lk_hit_test(self._ptr, rects, x, y)


class UI:
    """Top-level UI context wrapping lk_ui."""

    def __init__(self, lib_path=None):
        if lib_path is None:
            lib_path = os.path.join(
                os.path.dirname(__file__), "..", "..", "build", "liblk.so")
        self._lib = _load_lib(lib_path)
        self._ui = self._lib.lk_ui_create(None)
        if not self._ui:
            raise RuntimeError("lk_ui_create failed")
        self._intern = self._lib.lk_ui_intern(self._ui)
        # prevent GC of registered callbacks
        self._event_cb = None
        self._cmd_cb = None

    def destroy(self):
        if self._ui:
            self._lib.lk_ui_destroy(self._ui)
            self._ui = None

    def __del__(self):
        self.destroy()

    # -- frame lifecycle --

    def begin_frame(self):
        ptr = self._lib.lk_ui_begin_frame(self._ui)
        return Tree(self._lib, ptr, self._intern)

    def end_frame(self):
        """End frame, return list of Change objects."""
        cs = self._lib.lk_ui_end_frame(self._ui)
        count = self._lib.lk_changeset_count(cs)
        changes = []
        for i in range(count):
            cp = self._lib.lk_changeset_get(cs, i)
            c = cp.contents
            name = _intern_to_py(self._lib, self._intern, c.id)
            changes.append(Change(c.kind, name, c.node_ix))
        return changes

    def tree(self):
        """Return the current tree (after end_frame)."""
        ptr = self._lib.lk_ui_tree(self._ui)
        return Tree(self._lib, ptr, self._intern)

    # -- translators --

    def add_translator(self, event_type, ptype, command_name,
                       node_kind=0):
        self._lib.lk_ui_add_translator_s(
            self._ui, event_type,
            ptype.encode("utf-8") if ptype else None,
            node_kind,
            command_name.encode("utf-8"))

    # -- commands --

    def _read_cmd_args(self, cp):
        """Read command arguments using typed accessors."""
        lib = self._lib
        intern = self._intern
        argc = lib.lk_command_arg_count(cp)
        args = []
        for j in range(argc):
            tag = lib.lk_command_arg_tag(cp, j)
            if tag == ValueTag.I32:
                args.append((tag, lib.lk_command_arg_i32(cp, j)))
            elif tag == ValueTag.STR:
                sid = lib.lk_command_arg_str_id(cp, j)
                args.append((tag, _intern_to_py(lib, intern, sid)))
            elif tag == ValueTag.BOOL:
                args.append((tag, bool(lib.lk_command_arg_i32(cp, j))))
            else:
                args.append((tag, None))
        return args

    def commands(self):
        """Return list of Command objects from the current queue."""
        q = self._lib.lk_ui_commands(self._ui)
        count = self._lib.lk_command_queue_count(q)
        result = []
        for i in range(count):
            cp = self._lib.lk_command_queue_get(q, i)
            name_id = self._lib.lk_command_name(cp)
            name_str = _intern_to_py(self._lib, self._intern, name_id)
            args = self._read_cmd_args(cp)
            src_node = self._lib.lk_command_source_node(cp)
            src_ptype_id = self._lib.lk_command_source_ptype(cp)
            src_ptype_str = _intern_to_py(self._lib, self._intern,
                                           src_ptype_id)
            result.append(Command(name_str, args, src_node, src_ptype_str))
        return result

    def clear_commands(self):
        self._lib.lk_ui_clear_commands(self._ui)

    def set_command_handler(self, fn):
        """Register a Python command handler: fn(Command)."""
        ui = self

        @LK_COMMAND_HANDLER
        def _cb(cmd_ptr, ud):
            lib = ui._lib
            intern = ui._intern
            name_id = lib.lk_command_name(cmd_ptr)
            name = _intern_to_py(lib, intern, name_id)
            args = ui._read_cmd_args(cmd_ptr)
            src = lib.lk_command_source_node(cmd_ptr)
            ptype_id = lib.lk_command_source_ptype(cmd_ptr)
            ptype = _intern_to_py(lib, intern, ptype_id)
            cmd = Command(name, args, src, ptype)
            fn(cmd)

        self._cmd_cb = _cb  # prevent GC
        self._lib.lk_ui_set_command_handler(self._ui, _cb, None)

    # -- events --

    def make_event(self, event_type, **kwargs):
        """Create an lk_event. kwargs: x, y, button, keycode, mods, target."""
        ev = lk_event()
        if event_type in (Event.POINTER_MOVE, Event.POINTER_DOWN,
                          Event.POINTER_UP):
            self._lib.lk_event_init_pointer(
                byref(ev), event_type,
                kwargs.get("x", 0),
                kwargs.get("y", 0),
                kwargs.get("button", 1))
        elif event_type in (Event.KEY_DOWN, Event.KEY_UP):
            self._lib.lk_event_init_key(
                byref(ev), event_type,
                kwargs.get("keycode", 0),
                kwargs.get("mods", 0))
        else:
            ctypes.memset(byref(ev), 0, ctypes.sizeof(ev))
            ev.type = event_type
        if "target" in kwargs:
            ev.target = kwargs["target"]
        return ev

    def route_event(self, ev):
        """Route an event through the tree. Returns the event (check .handled)."""
        self._lib.lk_event_route(self._ui, byref(ev))
        return ev

    def set_event_handler(self, fn):
        """Register a Python event handler: fn(event, node_ix) -> bool."""
        @LK_EVENT_HANDLER
        def _cb(ev_ptr, node_ix, ud):
            return 1 if fn(ev_ptr.contents, node_ix) else 0

        self._event_cb = _cb  # prevent GC
        self._lib.lk_ui_set_event_handler(self._ui, _cb, None)

    # -- focus --

    def focus_next(self):
        cur = self._lib.lk_ui_tree(self._ui)
        return self._lib.lk_focus_next(self._ui, cur)

    def focus_prev(self):
        cur = self._lib.lk_ui_tree(self._ui)
        return self._lib.lk_focus_prev(self._ui, cur)

    def focus_current(self):
        cur = self._lib.lk_ui_tree(self._ui)
        return self._lib.lk_focus_current(self._ui, cur)

    def focus_set(self, node_id):
        cur = self._lib.lk_ui_tree(self._ui)
        return self._lib.lk_focus_set(self._ui, cur, node_id)

    def focus_clear(self):
        self._lib.lk_focus_clear(self._ui)

    # -- string interning --

    def intern(self, s):
        """Intern a string and return its node_id."""
        return self._lib.lk_intern_cid(
            self._intern, s.encode("utf-8"))

    def intern_str(self, nid):
        """Look up an interned ID and return a Python string."""
        return _intern_to_py(self._lib, self._intern, nid)
