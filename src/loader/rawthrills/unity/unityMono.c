// Mono's embedding API, as the player loaded it (see unity.h): the games'
// modules reach their managed code through it (their settings, their
// state, their calibration).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "unity.h"

typedef void MonoProperty, MonoMethodDesc, MonoAssembly, MonoClassField, MonoVTable, MonoDomain;

static struct
{
    void (*assemblyForeach)(void (*)(MonoAssembly *, void *), void *);
    MonoImage *(*assemblyImage)(MonoAssembly *);
    const char *(*imageName)(MonoImage *);
    MonoClass *(*classFromName)(MonoImage *, const char *, const char *);
    MonoClass *(*classGetParent)(MonoClass *);
    MonoMethod *(*methodFromName)(MonoClass *, const char *, int);
    MonoProperty *(*propertyFromName)(MonoClass *, const char *);
    MonoMethod *(*propertyGetter)(MonoProperty *);
    MonoMethodDesc *(*descNew)(const char *, int);
    MonoMethod *(*descSearch)(MonoMethodDesc *, MonoClass *);
    void (*descFree)(MonoMethodDesc *);
    MonoObject *(*invoke)(MonoMethod *, void *, void **, MonoObject **);
    void *(*unbox)(MonoObject *);
    MonoImage *(*imageLoaded)(const char *);
    MonoClassField *(*fieldFromName)(MonoClass *, const char *);
    MonoDomain *(*domainGet)(void);
    MonoVTable *(*classVtable)(MonoDomain *, MonoClass *);
    void (*fieldStaticGet)(MonoVTable *, MonoClassField *, void *);
    void (*fieldSet)(MonoObject *, MonoClassField *, void *);
    MonoClass *(*objectClass)(MonoObject *);
    void (*fieldGet)(MonoObject *, MonoClassField *, void *);
    uintptr_t (*arrayLength)(MonoObject *);
    char *(*arrayAddr)(MonoObject *, int, uintptr_t);
    MonoObject *(*stringNew)(MonoDomain *, const char *);
    char *(*stringToUtf8)(MonoObject *);
    void (*free)(void *);
} mono;

// Mono as loaded: through the dlopen of unityRt.c, else (Unity 2019 and
// later load it from UnityPlayer.so, past that hook) found among the
// loaded objects.
static int findMono(struct dl_phdr_info *info, size_t size, void *found)
{
    (void)size;
    void *h;
    // libmono-native.so and its kin are no runtime.
    if (!info->dlpi_name || !strstr(info->dlpi_name, "/libmono") ||
        !(h = dlopen(info->dlpi_name, RTLD_NOW | RTLD_NOLOAD)))
        return 0;
    if (!dlsym(h, "mono_runtime_invoke"))
    {
        dlclose(h);
        return 0;
    }
    *(void **)found = h;
    return 1;
}

static int monoLoad(void)
{
    void *h;
    if (mono.invoke)
        return 1;
    if (!unityMonoHandle)
        dl_iterate_phdr(findMono, &unityMonoHandle);
    h = unityMonoHandle ? unityMonoHandle : RTLD_DEFAULT;
    *(void **)&mono.assemblyForeach = dlsym(h, "mono_assembly_foreach");
    *(void **)&mono.assemblyImage = dlsym(h, "mono_assembly_get_image");
    *(void **)&mono.imageName = dlsym(h, "mono_image_get_name");
    *(void **)&mono.classFromName = dlsym(h, "mono_class_from_name");
    *(void **)&mono.classGetParent = dlsym(h, "mono_class_get_parent");
    *(void **)&mono.methodFromName = dlsym(h, "mono_class_get_method_from_name");
    *(void **)&mono.propertyFromName = dlsym(h, "mono_class_get_property_from_name");
    *(void **)&mono.propertyGetter = dlsym(h, "mono_property_get_get_method");
    *(void **)&mono.descNew = dlsym(h, "mono_method_desc_new");
    *(void **)&mono.descSearch = dlsym(h, "mono_method_desc_search_in_class");
    *(void **)&mono.descFree = dlsym(h, "mono_method_desc_free");
    *(void **)&mono.invoke = dlsym(h, "mono_runtime_invoke");
    *(void **)&mono.unbox = dlsym(h, "mono_object_unbox");
    *(void **)&mono.imageLoaded = dlsym(h, "mono_image_loaded");
    *(void **)&mono.fieldFromName = dlsym(h, "mono_class_get_field_from_name");
    *(void **)&mono.domainGet = dlsym(h, "mono_domain_get");
    *(void **)&mono.classVtable = dlsym(h, "mono_class_vtable");
    *(void **)&mono.fieldStaticGet = dlsym(h, "mono_field_static_get_value");
    *(void **)&mono.fieldSet = dlsym(h, "mono_field_set_value");
    *(void **)&mono.objectClass = dlsym(h, "mono_object_get_class");
    *(void **)&mono.fieldGet = dlsym(h, "mono_field_get_value");
    *(void **)&mono.arrayLength = dlsym(h, "mono_array_length");
    *(void **)&mono.arrayAddr = dlsym(h, "mono_array_addr_with_size");
    *(void **)&mono.stringNew = dlsym(h, "mono_string_new");
    *(void **)&mono.stringToUtf8 = dlsym(h, "mono_string_to_utf8");
    *(void **)&mono.free = dlsym(h, "mono_free");
    if (mono.assemblyForeach && mono.assemblyImage && mono.imageName && mono.classFromName && mono.classGetParent &&
        mono.methodFromName && mono.propertyFromName && mono.propertyGetter && mono.descNew && mono.descSearch &&
        mono.descFree && mono.invoke && mono.unbox)
        return 1;
    mono.invoke = NULL;
    return 0;
}

// An assembly's image by name (the game's code: Assembly-CSharp).
static struct
{
    const char *name;
    MonoImage *image;
} lookup;

static void findImage(MonoAssembly *assembly, void *unused)
{
    MonoImage *image = mono.assemblyImage(assembly);
    const char *name = image ? mono.imageName(image) : NULL;
    (void)unused;
    if (name && !strcmp(name, lookup.name))
        lookup.image = image;
}

// A static property's value (Instance).
static MonoObject *staticProperty(MonoClass *klass, const char *name)
{
    MonoProperty *p = klass ? mono.propertyFromName(klass, name) : NULL;
    MonoMethod *get = p ? mono.propertyGetter(p) : NULL;
    return get ? mono.invoke(get, NULL, NULL, NULL) : NULL;
}

static MonoMethod *method(MonoClass *klass, const char *desc)
{
    MonoMethodDesc *d = mono.descNew(desc, 0);
    MonoMethod *m = d ? mono.descSearch(d, klass) : NULL;
    if (d)
        mono.descFree(d);
    return m;
}

MonoImage *unityMonoImage(const char *name)
{
    lookup.name = name;
    lookup.image = NULL;
    if (!monoLoad())
        return NULL;
    // Unity 2021's Mono lists no assembly there: by the file it was
    // loaded from, <name>_Data/Managed beside the player's executable.
    if (mono.imageLoaded)
    {
        char exe[PATH_MAX], path[PATH_MAX * 2], *dot;
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0)
        {
            exe[n] = 0;
            if ((dot = strrchr(exe, '.')) && !strchr(dot, '/'))
                *dot = 0;
            snprintf(path, sizeof(path), "%s_Data/Managed/%s.dll", exe, name);
            if ((lookup.image = mono.imageLoaded(path)) || (lookup.image = mono.imageLoaded(name)))
                return lookup.image;
        }
    }
    mono.assemblyForeach(findImage, NULL);
    return lookup.image;
}

MonoImage *unityMonoGame(void)
{
    return unityMonoImage("Assembly-CSharp");
}

MonoMethod *unityMonoMethodIn(MonoImage *image, const char *nameSpace, const char *className, const char *desc)
{
    MonoClass *klass = image ? mono.classFromName(image, nameSpace, className) : NULL;
    return klass ? method(klass, desc) : NULL;
}

MonoMethod *unityMonoMethod(MonoImage *image, const char *className, const char *desc)
{
    return unityMonoMethodIn(image, "", className, desc);
}

MonoObject *unityMonoInvoke(MonoMethod *m, void *self, void **args, MonoObject **exc)
{
    return mono.invoke(m, self, args, exc);
}

int unityMonoUnboxInt(MonoObject *object)
{
    return *(int *)mono.unbox(object);
}

// The game's operator settings (TestPreferences, Nerf's and Marvel Contest
// of Champions' alike): a value, -1 if none; a new one, saved.
static MonoMethod *prefMethod(const char *desc, MonoObject **prefs)
{
    MonoImage *image = unityMonoGame();
    MonoClass *klass = image ? mono.classFromName(image, "", "TestPreferences") : NULL;
    *prefs = staticProperty(klass, "Instance");
    return *prefs ? method(klass, desc) : NULL;
}

int unityMonoPref(int pref)
{
    MonoObject *prefs, *exc = NULL, *value;
    MonoMethod *get = prefMethod("TestPreferences:Prefs_GetI(int)", &prefs);
    void *args[1] = {&pref};
    if (!get)
        return -1;
    value = mono.invoke(get, prefs, args, &exc);
    return exc || !value ? -1 : *(int *)mono.unbox(value);
}

void unityMonoSetPref(int pref, int value)
{
    MonoObject *prefs, *exc = NULL;
    MonoMethod *set = prefMethod("TestPreferences:Prefs_Set(int,int)", &prefs),
               *write = prefs ? prefMethod("TestPreferences:WritePrefs()", &prefs) : NULL;
    void *args[2] = {&pref, &value};
    if (!set || !write)
        return;
    mono.invoke(set, prefs, args, &exc);
    if (!exc)
        mono.invoke(write, prefs, NULL, &exc);
}

int unityMonoPrefString(int pref, char *out, size_t size)
{
    MonoObject *prefs, *exc = NULL, *value;
    MonoMethod *get = prefMethod("TestPreferences:Prefs_GetS(int)", &prefs);
    void *args[1] = {&pref};
    char *text;
    if (!get || !mono.stringToUtf8 || !mono.free || !size)
        return 0;
    value = mono.invoke(get, prefs, args, &exc);
    if (exc || !(text = value ? mono.stringToUtf8(value) : NULL))
    {
        *out = 0;
        return !exc;
    }
    snprintf(out, size, "%s", text);
    mono.free(text);
    return 1;
}

void unityMonoSetPrefString(int pref, const char *value)
{
    MonoObject *prefs, *exc = NULL, *text;
    MonoMethod *set = prefMethod("TestPreferences:Prefs_Set(int,string)", &prefs),
               *write = prefs ? prefMethod("TestPreferences:WritePrefs()", &prefs) : NULL;
    void *args[2] = {&pref, NULL};
    if (!set || !write || !mono.stringNew || !mono.domainGet || !(text = mono.stringNew(mono.domainGet(), value)))
        return;
    args[1] = text;
    mono.invoke(set, prefs, args, &exc);
    if (!exc)
        mono.invoke(write, prefs, NULL, &exc);
}

int unityMonoSetStaticBool(MonoImage *image, const char *nameSpace, const char *className, const char *staticField,
                          const char *boolField, int value)
{
    MonoClass *klass = image && monoLoad() && mono.fieldFromName && mono.domainGet && mono.classVtable &&
                               mono.fieldStaticGet && mono.fieldSet && mono.objectClass
                           ? mono.classFromName(image, nameSpace, className)
                           : NULL;
    MonoClassField *field = klass ? mono.fieldFromName(klass, staticField) : NULL, *flag;
    MonoVTable *vtable = field ? mono.classVtable(mono.domainGet(), klass) : NULL;
    MonoObject *object = NULL;
    uint8_t b = value != 0;
    if (!vtable)
        return 0;
    mono.fieldStaticGet(vtable, field, &object);
    if (!object || !(flag = mono.fieldFromName(mono.objectClass(object), boolField)))
        return 0;
    mono.fieldSet(object, flag, &b);
    return 1;
}

MonoObject *unityMonoStaticProperty(MonoImage *image, const char *nameSpace, const char *className, const char *name)
{
    return image && monoLoad() ? staticProperty(mono.classFromName(image, nameSpace, className), name) : NULL;
}

MonoObject *unityMonoSingleton(MonoImage *image, const char *nameSpace, const char *className)
{
    MonoClass *klass = image && monoLoad() ? mono.classFromName(image, nameSpace, className) : NULL;
    return klass ? staticProperty(mono.classGetParent(klass), "Instance") : NULL;
}

MonoObject *unityMonoStaticField(MonoImage *image, const char *nameSpace, const char *className, const char *name)
{
    MonoClass *klass = image && monoLoad() && mono.fieldFromName && mono.domainGet && mono.classVtable &&
                               mono.fieldStaticGet
                           ? mono.classFromName(image, nameSpace, className)
                           : NULL;
    MonoClassField *field = klass ? mono.fieldFromName(klass, name) : NULL;
    MonoVTable *vtable = field ? mono.classVtable(mono.domainGet(), klass) : NULL;
    MonoObject *object = NULL;
    if (vtable)
        mono.fieldStaticGet(vtable, field, &object);
    return object;
}

// An instance field, by name, read into value or written from it (its size:
// a reference, an int, a bool, as the field is).
static MonoClassField *instanceField(MonoObject *object, const char *name)
{
    return object && mono.fieldFromName && mono.objectClass ? mono.fieldFromName(mono.objectClass(object), name) : NULL;
}

int unityMonoGetField(MonoObject *object, const char *name, void *value)
{
    MonoClassField *field = mono.fieldGet ? instanceField(object, name) : NULL;
    if (!field)
        return 0;
    mono.fieldGet(object, field, value);
    return 1;
}

int unityMonoSetField(MonoObject *object, const char *name, void *value)
{
    MonoClassField *field = mono.fieldSet ? instanceField(object, name) : NULL;
    if (!field)
        return 0;
    mono.fieldSet(object, field, value);
    return 1;
}

MonoObject *unityMonoArrayItem(MonoObject *array, int index)
{
    if (!array || !mono.arrayLength || !mono.arrayAddr || index < 0 || (uintptr_t)index >= mono.arrayLength(array))
        return NULL;
    return *(MonoObject **)mono.arrayAddr(array, sizeof(MonoObject *), index);
}

int unityMonoCallInt(MonoObject *object, const char *desc, int *value)
{
    MonoMethod *m = object && mono.objectClass ? method(mono.objectClass(object), desc) : NULL;
    MonoObject *exc = NULL, *ret = m ? mono.invoke(m, object, NULL, &exc) : NULL;
    if (!ret || exc)
        return 0;
    *value = *(int *)mono.unbox(ret);
    return 1;
}

// A method of the object called with those arguments: its result (NULL
// for none, or when it threw: *ok says which).
MonoObject *unityMonoCall(MonoObject *object, const char *desc, void **args, int *ok)
{
    MonoMethod *m = object && mono.objectClass ? method(mono.objectClass(object), desc) : NULL;
    MonoObject *exc = NULL, *ret = m ? mono.invoke(m, object, args, &exc) : NULL;
    if (ok)
        *ok = m && !exc;
    return exc ? NULL : ret;
}

// A managed string of that text.
MonoObject *unityMonoString(const char *text)
{
    return mono.stringNew && mono.domainGet ? mono.stringNew(mono.domainGet(), text) : NULL;
}
