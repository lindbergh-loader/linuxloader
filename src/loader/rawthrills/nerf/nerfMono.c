// The guns' calibration. The game scales each ADC by the range its test
// menu stored (preferences 67..74: P1 min X, max X, min Y, max Y, then P2),
// and without one it opens the test menu at boot to have the guns
// calibrated. The aim here is absolute already, on the RIO's full scale
// (nerfRio.c): that range is written into the preferences, through Mono's
// embedding API, and the game reads it again
// (IOManager.GrabCalibrationFromPreferences).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "nerf.h"

typedef void MonoProperty, MonoMethodDesc, MonoAssembly;

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
} mono;

static int monoLoad(void)
{
    void *h = nerfMonoHandle ? nerfMonoHandle : RTLD_DEFAULT;
    if (mono.invoke)
        return 1;
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
    if (mono.assemblyForeach && mono.assemblyImage && mono.imageName && mono.classFromName && mono.classGetParent &&
        mono.methodFromName && mono.propertyFromName && mono.propertyGetter && mono.descNew && mono.descSearch &&
        mono.descFree && mono.invoke && mono.unbox)
        return 1;
    mono.invoke = NULL;
    return 0;
}

// The game's code: Assembly-CSharp.
static void findGame(MonoAssembly *assembly, void *found)
{
    MonoImage *image = mono.assemblyImage(assembly);
    const char *name = image ? mono.imageName(image) : NULL;
    if (name && !strcmp(name, "Assembly-CSharp"))
        *(MonoImage **)found = image;
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

MonoImage *nerfMonoGame(void)
{
    MonoImage *image = NULL;
    if (monoLoad())
        mono.assemblyForeach(findGame, &image);
    return image;
}

MonoMethod *nerfMonoMethod(MonoImage *image, const char *className, const char *desc)
{
    MonoClass *klass = image ? mono.classFromName(image, "", className) : NULL;
    return klass ? method(klass, desc) : NULL;
}

MonoObject *nerfMonoInvoke(MonoMethod *m, void *self, void **args, MonoObject **exc)
{
    return mono.invoke(m, self, args, exc);
}

int nerfMonoUnboxInt(MonoObject *object)
{
    return *(int *)mono.unbox(object);
}

void nerfForceCalibration(void)
{
    // P1 X min, max, Y min, max; P2 the same.
    static const int range[8] = {0, NERF_ADC_MAX, 0, NERF_ADC_MAX, 0, NERF_ADC_MAX, 0, NERF_ADC_MAX};
    MonoImage *image = nerfMonoGame();
    MonoClass *prefsClass, *ioClass;
    MonoObject *prefs, *io, *exc = NULL;
    MonoMethod *get, *set, *write, *grab;
    int changed = 0;

    if (!image)
    {
        nerfLog("no Mono runtime, the guns stay uncalibrated\n");
        return;
    }
    prefsClass = mono.classFromName(image, "", "TestPreferences");
    ioClass = mono.classFromName(image, "", "IOManager");
    prefs = staticProperty(prefsClass, "Instance");
    // IOManager's Instance is SingletonMonoBehaviour<IOManager>'s.
    io = ioClass ? staticProperty(mono.classGetParent(ioClass), "Instance") : NULL;
    get = prefsClass ? method(prefsClass, "TestPreferences:Prefs_GetI(int)") : NULL;
    set = prefsClass ? method(prefsClass, "TestPreferences:Prefs_Set(int,int)") : NULL;
    write = prefsClass ? method(prefsClass, "TestPreferences:WritePrefs()") : NULL;
    grab = ioClass ? method(ioClass, "IOManager:GrabCalibrationFromPreferences()") : NULL;
    if (!prefs || !io || !get || !set || !write || !grab)
    {
        nerfLog("calibration: the game's classes not found, the guns stay uncalibrated\n");
        return;
    }
    for (int i = 0; i < 8; i++)
    {
        int pref = 67 + i, value = range[i];
        void *args[2] = {&pref, &value};
        MonoObject *current = mono.invoke(get, prefs, args, &exc);
        if (exc || !current)
            break;
        if (*(int *)mono.unbox(current) == value)
            continue;
        mono.invoke(set, prefs, args, &exc);
        changed = 1;
    }
    if (exc)
    {
        nerfLog("calibration: the game threw, the guns stay as they are\n");
        return;
    }
    if (changed)
        mono.invoke(write, prefs, NULL, NULL);
    mono.invoke(grab, io, NULL, NULL);
    nerfLog("calibration %s (0..%d)\n", changed ? "set" : "kept", NERF_ADC_MAX);
}
