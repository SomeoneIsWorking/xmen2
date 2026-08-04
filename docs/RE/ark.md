# ARK — Alchemy's meta-object registration system

Reverse-engineered from `libIGDisplay.dll` (Ghidra, `tools/ghidra_scripts/DecompArk.py`),
cross-checked against the Alchemy 5.0 headers (`igCore/igObjectMacros.h`).
This is the mechanism a replacement `libIG*.dll` must speak to be accepted by the
original `libIGCore.dll`.

Every class the `IG_OBJECT_DEFINE` / `IG_ABSTRACT_OBJECT_DEFINE` macro touches
gets the same five static functions. All the per-class variation is in the
arguments to one libIGCore call.

## The five functions, as they actually compile

```c
/* Construction is entirely delegated to libIGCore. Identical for EVERY class. */
T *T::_instantiateFromPool(igMemoryPool *pool)
{
    arkRegister();
    return (T *)igMetaObject::createInstance(_Meta, pool);
}

/* Idempotent registration trigger. Identical for every class. */
void T::arkRegister(void)
{
    igArkRegister(T::arkRegisterInternal);          /* 1-arg overload */
}

/* Where the class actually describes itself. This is the interesting one. */
__internalFunctionList *T::arkRegisterInternal(void)
{
    _libIGDisplay_Init();                           /* per-module init */
    return igArkRegister(                           /* 11-arg overload */
        isAbstract,          /* igBool */
        &_Meta,              /* igMetaObject** — libIGCore fills this in */
        Base::arkRegisterInternal,
        Base::getClassMeta,
        T::getClassMetaSafe,
        "T",                 /* class name, as a plain C string */
        instanceSize,        /* bytes */
        T::retrieveVTablePointer,   /* NULL when abstract */
        T::arkRegisterInitialize,
        T::arkRegisterUser,         /* NULL unless the class needs one */
        dependentArkRegisters);     /* NULL-terminated fn-ptr array, or NULL */
}

/* Lazy meta allocation, used before registration has run. */
igMetaObject *T::getClassTypeLazy(void)
{
    if (_Meta == NULL)
        _Meta = igMetaObject::_instantiateFromPool(
                    igGetMemoryPool(igMemoryPoolMetaData));
    return _Meta;
}

/* Reflection fields + the abstract->concrete binding. Per-class. */
static void T::arkRegisterInitialize(void) { ... }
```

### The three observed argument sets

| class | abstract | size | vtable retriever | user hook | deps |
|---|---|---|---|---|---|
| `igWindow` | true | `0x08` | NULL | NULL | NULL |
| `igControllerManager` | true | `0x0c` | NULL | NULL | non-NULL |
| `igWin32Window` | **false** | `0x6c` | **supplied** | **supplied** | non-NULL |

The pattern is consistent: **abstract classes pass NULL for the vtable
retriever, concrete ones supply it.**

## `retrieveVTablePointer` — why we do NOT have to reverse vtable layout

```c
void *igWin32Window::retrieveVTablePointer(void)
{
    char tmp[0x6c];
    igObject::igObject((igObject *)tmp);        /* construct a throwaway */
    *(void **)tmp = &igWin32Window::_vftable_;  /* stamp our vtable in */
    ...
    return *(void **)(tmp + *(int *)(*(int *)ArkCore + 0x394));
}
```

Alchemy builds a temporary instance, writes the class's vtable pointer into it,
and reads it back at an offset that `igArkCore` stores at `+0x394` — i.e. the
compiler's vptr offset, discovered once at runtime rather than assumed.

**Consequence for this project:** a replacement class does not need its vtable to
sit at any particular offset or in MSVC's layout. It needs to hand libIGCore a
pointer to *a* vtable via this hook, and libIGCore stamps that pointer into every
instance it creates. Hand-rolled C vtables are therefore sufficient, and the
`vtable` frontier step is much cheaper than it looked — **provided** the *slot
order within* the vtable still matches what callers expect, which this does not
address (see the open question below).

## `arkRegisterInitialize` — reflection fields, and the platform binding

For a class with serialisable fields it appends them to the meta object:

```c
void igControllerManager::arkRegisterInitialize(void)
{
    int base = igMetaObject::getMetaFieldCount(_Meta);
    igMetaObject::instantiateAndAppendFields(_Meta, &igObjectRefMetaField_instantiate, 1);
    igMetaField *f = igMetaObject::getIndexedMetaField(_Meta, base);
    *(igMetaObject **)(f + 0x38) = igControllerList::getClassTypeLazy();  /* field type   */
    f[0x34] = 1;                                                          /* flag         */
    igMetaObject::setMetaFieldBasicPropertiesAndValidateAll(
        _Meta, &"_controllers", &"k_controllers", &offsets, base);

    *(void **)(_Meta + 0x3c) = igWin32ControllerManager::getClassMetaSafe;   /* <-- */
}
```

That last line is the important one, and `igWindow` has the same shape:

```c
void igWindow::arkRegisterInitialize(void)
{
    *(void **)(_Meta + 0x3c) = igWin32Window::getClassMetaSafe;
}
```

**`_Meta + 0x3c` is where an abstract class records which concrete class
implements it on this platform.** Both observed abstract classes write the
`igWin32*` implementation there, and both concrete classes are the Win32
back-ends of exactly those abstractions.

This makes `_Meta+0x3c` **the substitution point for the whole input and window
layer**: register a native `igSDLControllerManager` and repoint that slot,
instead of replacing `libIGDisplay` wholesale.

That reading is **verified**, not inferred — by decompiling `libIGCore.dll` — `igMetaObject::createInstance`
at `0x10044380` follows `+0x3c` in a loop until it reaches a concrete meta:

```c
igObject *igMetaObject::createInstance(igMemoryPool *pool) const
{
    /* Walk the implementation chain: an abstract meta points at the meta of
       the class that implements it on this platform, transitively. */
    igMetaObject *(*impl)(void) = *(void **)(this + 0x3c);
    while (impl != NULL) {
        this = impl();
        impl = *(void **)(this + 0x3c);
    }
    if (this[0x1a] == 1)                 /* still abstract -> refuse */
        return NULL;

    if (pool == NULL) {                  /* pool named on the meta, else current */
        const char *name = *(char **)(this + 0x60);
        pool = (name && *name) ? igGetMemoryPool(name) : NULL;
        if (!pool) pool = igMemoryPool::getCurrentMemoryPool();
    }
    int prefix = *(int *)(this + 0x20);
    void *raw  = pool->vtbl[0xcc / 4](*(int *)(this + 0x48) + prefix);
    igObject *obj = (igObject *)((char *)raw + prefix);
    if (obj) igObject::constructDerived(obj, this);
    return obj;
}
```

So the substitution plan is sound: **repointing `_Meta+0x3c` redirects every
instantiation of an abstract class to a different concrete implementation**, and
libIGCore does the walking for us.

### `igMetaObject` field offsets learned from this function

| offset | meaning |
|---|---|
| `+0x1a` | `isAbstract` byte — `1` means `createInstance` refuses |
| `+0x20` | allocation prefix / alignment added before the object |
| `+0x3c` | implementation redirect: `igMetaObject *(*)(void)`, NULL when concrete |
| `+0x48` | instance size in bytes (the `8` / `0xc` / `0x6c` passed to `igArkRegister`) |
| `+0x60` | memory-pool name (`char *`), used when the caller passes no pool |

Allocation goes through the memory pool's vtable slot `0xcc`, and
`igObject::constructDerived(obj, meta)` finishes construction — that is where the
vtable pointer captured by `retrieveVTablePointer` gets stamped into the object.

## `igArkRegister` — the two overloads

Ghidra's demangler recovers both signatures from the mangled names directly, so
these are ground truth rather than inference:

```c
/* 1-arg: run a class's registrar, then run every dependency it returned. */
void igArkRegister(__internalFunctionList *(*registrar)(void))
{
    __internalFunctionList *deps = registrar();
    if (deps) {
        for (int i = 0; i < deps->count; i++)
            deps->fns[i]();
        delete deps;
    }
}

/* 11-arg: the class description itself. */
__internalFunctionList *igArkRegister(
    bool                       isAbstract,
    igMetaObject             **metaSlot,
    __internalFunctionList *(*parentRegisterInternal)(void),
    igMetaObject            *(*parentGetClassMeta)(void),
    igMetaObject            *(*getClassMetaSafe)(void),
    const char                *className,
    int                        instanceSize,
    void                    *(*retrieveVTablePointer)(void),
    void                     (*arkRegisterInitialize)(void),
    void                     (*arkRegisterUser)(void),
    void                    (**dependentArkRegisters)(void));
```

It allocates the meta object from the `igMemoryPoolMetaData` pool if `*metaSlot`
is NULL, naming it `"igMetaObject: <className>"`.

## The libIGCore API a replacement DLL must call

From the external-call lists of the functions above:

- `igArkRegister` (both the 1-arg and 11-arg overloads)
- `igMetaObject::createInstance`
- `igMetaObject::_instantiateFromPool`
- `igGetMemoryPool`
- `igMetaObject::getMetaFieldCount`
- `igMetaObject::getIndexedMetaField`
- `igMetaObject::instantiateAndAppendFields`
- `igMetaObject::setMetaFieldBasicPropertiesAndValidateAll`
- `igMetaObject::registerClassDestructor`, `setDefault` (seen in `igWin32Window`)
- `igObject::igObject` (the base constructor, for `retrieveVTablePointer`)

All are exported by name from `libIGCore.dll`, so a mingw-built DLL can import
them via a generated import library.

## Open questions

1. `igObject::constructDerived` — not yet read. It is where the vtable pointer
   is stamped and where per-class construction happens; needed before we can
   hand libIGCore a class of our own.
2. `igWin32Window::arkRegisterInitialize` writes ~28 consecutive function
   pointers into a table before registering; what is that table? (likely the
   per-field instantiators for its many meta fields)
4. Does the **slot order** inside a class's vtable have to match MSVC's, given
   that only the vtable *pointer* is handed over? Callers that dispatch
   virtually index by slot, so almost certainly yes — needs the layout read out
   of the binary (`tools/ghidra_scripts/DumpVtab.py`).
