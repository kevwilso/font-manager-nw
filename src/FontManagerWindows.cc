#define WINVER 0x0600
#include "FontDescriptor.h"
#include <dwrite.h>

// throws a JS error when there is some exception in DirectWrite
#define HR(hr) \
  if (FAILED(hr)) throw "Font loading error";

WCHAR *utf8ToUtf16(const char *input) {
  unsigned int len = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
  WCHAR *output = new WCHAR[len];
  MultiByteToWideChar(CP_UTF8, 0, input, -1, output, len);
  return output;
}

char *utf16ToUtf8(const WCHAR *input) {
  unsigned int len = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
  char *output = new char[len];
  WideCharToMultiByte(CP_UTF8, 0, input, -1, output, len, NULL, NULL);
  return output;
}

// returns the index of the user's locale in the set of localized strings
unsigned int getLocaleIndex(IDWriteLocalizedStrings *strings) {
  unsigned int index = 0;
  BOOL exists = false;
  wchar_t localeName[LOCALE_NAME_MAX_LENGTH];

  // Get the default locale for this user.
  int success = GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH);

  // If the default locale is returned, find that locale name, otherwise use "en-us".
  if (success) {
    HR(strings->FindLocaleName(localeName, &index, &exists));
  }

  // if the above find did not find a match, retry with US English
  if (!exists) {
    HR(strings->FindLocaleName(L"en-us", &index, &exists));
  }

  if (!exists)
    index = 0;

  return index;
}

// gets a localized string for a font
char *getString(IDWriteFont *font, DWRITE_INFORMATIONAL_STRING_ID string_id) {
  IDWriteLocalizedStrings *strings = NULL;

  BOOL exists = false;
  HR(font->GetInformationalStrings(
    string_id,
    &strings,
    &exists
  ));

  unsigned int index = getLocaleIndex(strings);

  if (exists) {
    unsigned int len = 0;
    WCHAR *str = NULL;

    HR(strings->GetStringLength(index, &len));
    str = new WCHAR[len + 1];

    HR(strings->GetString(index, str, len + 1));

    // convert to utf8
    char *res = utf16ToUtf8(str);
    delete str;
    return res;
  }

  return "";
}

FontDescriptor *resultFromFont(IDWriteFont *font) {
  FontDescriptor *res = NULL;
  IDWriteFontFace *face = NULL;
  unsigned int numFiles = 0;

  HR(font->CreateFontFace(&face));

  // get the font files from this font face
  IDWriteFontFile *files = NULL;
  HR(face->GetFiles(&numFiles, NULL));
  HR(face->GetFiles(&numFiles, &files));

  // return the first one
  if (numFiles > 0) {
    IDWriteFontFileLoader *loader = NULL;
    IDWriteLocalFontFileLoader *fileLoader = NULL;
    unsigned int nameLength = 0;
    const void *referenceKey = NULL;
    unsigned int referenceKeySize = 0;
    WCHAR *name = NULL;

    HR(files[0].GetLoader(&loader));

    // check if this is a local font file
    HRESULT hr = loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader), (void **)&fileLoader);
    if (SUCCEEDED(hr)) {
      // get the file path
      HR(files[0].GetReferenceKey(&referenceKey, &referenceKeySize));
      HR(fileLoader->GetFilePathLengthFromKey(referenceKey, referenceKeySize, &nameLength));

      name = new WCHAR[nameLength + 1];
      HR(fileLoader->GetFilePathFromKey(referenceKey, referenceKeySize, name, nameLength + 1));

      char *postscriptName = getString(font, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
      char *family = getString(font, DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES);
      char *style = getString(font, DWRITE_INFORMATIONAL_STRING_WIN32_SUBFAMILY_NAMES);

      res = new FontDescriptor(
        utf16ToUtf8(name),
        postscriptName,
        family,
        style,
        (FontWeight) font->GetWeight(),
        (FontWidth) font->GetStretch(),
        font->GetStyle() == DWRITE_FONT_STYLE_ITALIC,
        false // TODO!
      );

      delete name;
      delete postscriptName;
      delete family;
      delete style;
    }
  }

  return res;
}

ResultSet *getAvailableFonts() {
  ResultSet *res = new ResultSet();
  int count = 0;

  IDWriteFactory *factory = NULL;
  HR(DWriteCreateFactory(
    DWRITE_FACTORY_TYPE_SHARED,
    __uuidof(IDWriteFactory),
    reinterpret_cast<IUnknown**>(&factory)
  ));

  // Get the system font collection.
  IDWriteFontCollection *collection = NULL;
  HR(factory->GetSystemFontCollection(&collection));

  // Get the number of font families in the collection.
  int familyCount = collection->GetFontFamilyCount();

  for (int i = 0; i < familyCount; i++) {
    IDWriteFontFamily *family = NULL;
    int fontCount = 0;

    // Get the font family.
    HR(collection->GetFontFamily(i, &family));
    fontCount = family->GetFontCount();

    for (int j = 0; j < fontCount; j++) {
      IDWriteFont *font = NULL;
      HR(family->GetFont(j, &font));
      res->push_back(resultFromFont(font));
    }
  }

  return res;
}

IDWriteFontList *findFontsByFamily(IDWriteFontCollection *collection, FontDescriptor *desc) {
  WCHAR *family = utf8ToUtf16(desc->family);

  unsigned int index;
  BOOL exists;
  HR(collection->FindFamilyName(family, &index, &exists));
  delete family;

  if (exists) {
    IDWriteFontFamily *family = NULL;
    HR(collection->GetFontFamily(index, &family));

    IDWriteFontList *fontList = NULL;
    HR(family->GetMatchingFonts(
      desc->weight ? (DWRITE_FONT_WEIGHT) desc->weight : DWRITE_FONT_WEIGHT_NORMAL,
      desc->width  ? (DWRITE_FONT_STRETCH) desc->width : DWRITE_FONT_STRETCH_UNDEFINED,
      desc->italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
      &fontList
    ));

    return fontList;
  }

  return NULL;
}

IDWriteFont *findFontByPostscriptName(IDWriteFontCollection *collection, FontDescriptor *desc) {
  // Get the number of font families in the collection.
  int familyCount = collection->GetFontFamilyCount();

  for (int i = 0; i < familyCount; i++) {
    IDWriteFontFamily *family = NULL;
    int fontCount = 0;

    // Get the font family.
    HR(collection->GetFontFamily(i, &family));
    fontCount = family->GetFontCount();

    for (int j = 0; j < fontCount; j++) {
      IDWriteFont *font = NULL;
      HR(family->GetFont(j, &font));

      char *psName = getString(font, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
      if (strcmp(psName, desc->postscriptName) == 0) {
        delete psName;
        return font;
      }

      delete psName;
    }
  }

  return NULL;
}

// TODO: style, monospace
ResultSet *findFonts(FontDescriptor *desc) {
  ResultSet *res = new ResultSet();
  
  IDWriteFactory *factory = NULL;
  HR(DWriteCreateFactory(
    DWRITE_FACTORY_TYPE_SHARED,
    __uuidof(IDWriteFactory),
    reinterpret_cast<IUnknown**>(&factory)
  ));

  // Get the system font collection.
  IDWriteFontCollection *collection = NULL;
  HR(factory->GetSystemFontCollection(&collection));

  if (desc->family) {
    IDWriteFontList *fonts = findFontsByFamily(collection, desc);
    int fontCount = fonts ? fonts->GetFontCount() : 0;

    for (int j = 0; j < fontCount; j++) {
      IDWriteFont *font = NULL;
      HR(fonts->GetFont(j, &font));
      res->push_back(resultFromFont(font));
    }

    return res;
  } else if (desc->postscriptName) {
    IDWriteFont *font = findFontByPostscriptName(collection, desc);
    res->push_back(resultFromFont(font));
    return res;
  }

  return res;
}

FontDescriptor *findFont(FontDescriptor *desc) {
  IDWriteFactory *factory = NULL;
  HR(DWriteCreateFactory(
    DWRITE_FACTORY_TYPE_SHARED,
    __uuidof(IDWriteFactory),
    reinterpret_cast<IUnknown**>(&factory)
  ));

  // Get the system font collection.
  IDWriteFontCollection *collection = NULL;
  HR(factory->GetSystemFontCollection(&collection));

  IDWriteFont *font = NULL;
  if (desc->family) {
    IDWriteFontList *fonts = findFontsByFamily(collection, desc);
    if (fonts && fonts->GetFontCount() > 0)
      fonts->GetFont(0, &font);
  } else if (desc->postscriptName) {
    font = findFontByPostscriptName(collection, desc);
  }

  if (font) {
    return resultFromFont(font);
  }

  return NULL;
}

// custom text renderer used to determine the fallback font for a given char
class FontFallbackRenderer : public IDWriteTextRenderer {
public:
  IDWriteFontCollection *systemFonts;
  IDWriteFont *font;
  unsigned long refCount;

  FontFallbackRenderer(IDWriteFontCollection *collection) {
    refCount = 0;
    collection->AddRef();
    systemFonts = collection;
    font = NULL;
  }

  ~FontFallbackRenderer() {
    if (systemFonts)
      systemFonts->Release();

    if (font)
      font->Release();
  }

  // IDWriteTextRenderer methods
  IFACEMETHOD(DrawGlyphRun)(
      void *clientDrawingContext,
      FLOAT baselineOriginX,
      FLOAT baselineOriginY,
      DWRITE_MEASURING_MODE measuringMode,
      DWRITE_GLYPH_RUN const *glyphRun,
      DWRITE_GLYPH_RUN_DESCRIPTION const *glyphRunDescription,
      IUnknown *clientDrawingEffect) {

    // save the font that was actually rendered
    return systemFonts->GetFontFromFontFace(glyphRun->fontFace, &font);
  }

  IFACEMETHOD(DrawUnderline)(
      void *clientDrawingContext,
      FLOAT baselineOriginX,
      FLOAT baselineOriginY,
      DWRITE_UNDERLINE const *underline,
      IUnknown *clientDrawingEffect) {
    return E_NOTIMPL;
  }


  IFACEMETHOD(DrawStrikethrough)(
      void *clientDrawingContext,
      FLOAT baselineOriginX,
      FLOAT baselineOriginY,
      DWRITE_STRIKETHROUGH const *strikethrough,
      IUnknown *clientDrawingEffect) {
    return E_NOTIMPL;
  }


  IFACEMETHOD(DrawInlineObject)(
      void *clientDrawingContext,
      FLOAT originX,
      FLOAT originY,
      IDWriteInlineObject *inlineObject,
      BOOL isSideways,
      BOOL isRightToLeft,
      IUnknown *clientDrawingEffect) {
    return E_NOTIMPL;
  }

  // IDWritePixelSnapping methods
  IFACEMETHOD(IsPixelSnappingDisabled)(void *clientDrawingContext, BOOL *isDisabled) {
    *isDisabled = FALSE;
    return S_OK;
  }

  IFACEMETHOD(GetCurrentTransform)(void *clientDrawingContext, DWRITE_MATRIX *transform) {
    const DWRITE_MATRIX ident = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    *transform = ident;
    return S_OK;
  }

  IFACEMETHOD(GetPixelsPerDip)(void *clientDrawingContext, FLOAT *pixelsPerDip) {
    *pixelsPerDip = 1.0f;
    return S_OK;
  }

  // IUnknown methods
  IFACEMETHOD_(unsigned long, AddRef)() {
    return InterlockedIncrement(&refCount);
  }

  IFACEMETHOD_(unsigned long,  Release)() {
    unsigned long newCount = InterlockedDecrement(&refCount);
    if (newCount == 0) {
      delete this;
      return 0;
    }

    return newCount;
  }

  IFACEMETHOD(QueryInterface)(IID const& riid, void **ppvObject) {
    if (__uuidof(IDWriteTextRenderer) == riid) {
      *ppvObject = this;
    } else if (__uuidof(IDWritePixelSnapping) == riid) {
      *ppvObject = this;
    } else if (__uuidof(IUnknown) == riid) {
      *ppvObject = this;
    } else {
      *ppvObject = nullptr;
      return E_FAIL;
    }

    this->AddRef();
    return S_OK;
  }
};

FontDescriptor *substituteFont(char *postscriptName, char *string) {
  FontDescriptor *res = NULL;

  IDWriteFactory *factory = NULL;
  HR(DWriteCreateFactory(
    DWRITE_FACTORY_TYPE_SHARED,
    __uuidof(IDWriteFactory),
    reinterpret_cast<IUnknown**>(&factory)
  ));

  // Get the system font collection.
  IDWriteFontCollection *collection = NULL;
  HR(factory->GetSystemFontCollection(&collection));

  // find the font for the given postscript name
  FontDescriptor *desc = new FontDescriptor();
  desc->postscriptName = postscriptName;
  IDWriteFont *font = findFontByPostscriptName(collection, desc);

  if (font) {
    // get the font family name
    IDWriteFontFamily *family = NULL;
    HR(font->GetFontFamily(&family));

    IDWriteLocalizedStrings *names = NULL;
    HR(family->GetFamilyNames(&names));

    unsigned int length = 0;
    HR(names->GetStringLength(0, &length));
    WCHAR *familyName = new WCHAR[length + 1];
    HR(names->GetString(0, familyName, length + 1));

    // convert utf8 string for substitution to utf16
    WCHAR *str = utf8ToUtf16(string);

    // create a text format
    IDWriteTextFormat *format = NULL;
    HR(factory->CreateTextFormat(
      familyName,
      collection,
      font->GetWeight(),
      font->GetStyle(),
      font->GetStretch(),
      12.0,
      L"en-us",
      &format
    ));

    // create a text layout for the substitution string
    IDWriteTextLayout *layout = NULL;
    HR(factory->CreateTextLayout(
      str,
      wcslen(str),
      format,
      100.0,
      100.0,
      &layout
    ));

    // render it using a custom renderer that saves the physical font being used
    FontFallbackRenderer *renderer = new FontFallbackRenderer(collection);
    HR(layout->Draw(NULL, renderer, 100.0, 100.0));

    // if we found something, create a result object
    if (renderer->font) {
      res = resultFromFont(renderer->font);
    }

    // free all the things
    delete renderer;
    layout->Release();
    format->Release();
    delete str;
    delete familyName;
    names->Release();
    family->Release();
    font->Release();
  }

  desc->postscriptName = NULL;
  delete desc;
  collection->Release();
  factory->Release();

  return res;
}
