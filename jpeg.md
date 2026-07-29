[Jump to content](#bodyContent)

   Main menu

Main menu
 move to sidebar hide

 Navigation

- [Main page](/wiki/Main_Page)
- [Contents](/wiki/Wikipedia:Contents)
- [Current events](/wiki/Portal:Current_events)
- [Random article](/wiki/Special:Random)
- [About Wikipedia](/wiki/Wikipedia:About)
- [Contact us](//en.wikipedia.org/wiki/Wikipedia:Contact_us)

 Contribute

- [Help](/wiki/Help:Contents)
- [Learn to edit](/wiki/Help:Introduction)
- [Community portal](/wiki/Wikipedia:Community_portal)
- [Recent changes](/wiki/Special:RecentChanges)
- [Upload file](/wiki/Wikipedia:File_upload_wizard)
- [Special pages](/wiki/Special:SpecialPages)

  [](/wiki/Main_Page)

 [Search](/wiki/Special:Search)

 Search

   Appearance

- [Donate](https://donate.wikimedia.org/?wmf_source=donate&wmf_medium=sidebar&wmf_campaign=en.wikipedia.org&uselang=en)

- [Create account](/w/index.php?title=Special:CreateAccount&returnto=JPEG+File+Interchange+Format)

- [Log in](/w/index.php?title=Special:UserLogin&returnto=JPEG+File+Interchange+Format)

   Personal tools

- [Donate](https://donate.wikimedia.org/?wmf_source=donate&wmf_medium=sidebar&wmf_campaign=en.wikipedia.org&uselang=en)

- [Create account](/w/index.php?title=Special:CreateAccount&returnto=JPEG+File+Interchange+Format)

- [Log in](/w/index.php?title=Special:UserLogin&returnto=JPEG+File+Interchange+Format)

## Contents

 move to sidebar hide

-  [(Top)](#)

-  [1 Purpose](#Purpose)   Toggle Purpose subsection

  -  [1.1 Component sample registration](#Component_sample_registration)

  -  [1.2 Resolution and aspect ratio](#Resolution_and_aspect_ratio)

  -  [1.3 Color space](#Color_space)

-  [2 File format structure](#File_format_structure)   Toggle File format structure subsection

  -  [2.1 JFIF APP0 marker segment](#JFIF_APP0_marker_segment)

  -  [2.2 JFIF extension APP0 marker segment](#JFIF_extension_APP0_marker_segment)

-  [3 Compatibility](#Compatibility)

-  [4 History](#History)

-  [5 See also](#See_also)

-  [6 References](#References)

-  [7 Further reading](#Further_reading)   Toggle Further reading subsection

  -  [7.1 Books](#Books)

  -  [7.2 Standards](#Standards)

   Toggle the table of contents

# JPEG File Interchange Format

   8 languages

- [Català](https://ca.wikipedia.org/wiki/JPEG_File_Interchange_Format)
- [Deutsch](https://de.wikipedia.org/wiki/JPEG_File_Interchange_Format)
- [Español](https://es.wikipedia.org/wiki/JFIF)
- [Français](https://fr.wikipedia.org/wiki/JPEG_File_Interchange_Format)
- [日本語](https://ja.wikipedia.org/wiki/JPEG_File_Interchange_Format)
- [한국어](https://ko.wikipedia.org/wiki/JFIF)
- [Português](https://pt.wikipedia.org/wiki/JPEG_File_Interchange_Format)
- [中文](https://zh.wikipedia.org/wiki/JPEG%E6%96%87%E4%BB%B6%E4%BA%A4%E6%8D%A2%E6%A0%BC%E5%BC%8F)

[Edit links](https://www.wikidata.org/wiki/Special:EntityPage/Q26329975#sitelinks-wikipedia)

- [Article](/wiki/JPEG_File_Interchange_Format)

- [Talk](/wiki/Talk:JPEG_File_Interchange_Format)

  English

- [Read](/wiki/JPEG_File_Interchange_Format)

- [Edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit)

- [View history](/w/index.php?title=JPEG_File_Interchange_Format&action=history)

   Tools

Tools
 move to sidebar hide

 Actions

- [Read](/wiki/JPEG_File_Interchange_Format)

- [Edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit)

- [View history](/w/index.php?title=JPEG_File_Interchange_Format&action=history)

 General

- [What links here](/wiki/Special:WhatLinksHere/JPEG_File_Interchange_Format)
- [Related changes](/wiki/Special:RecentChangesLinked/JPEG_File_Interchange_Format)
- [Upload file](//en.wikipedia.org/wiki/Wikipedia:File_Upload_Wizard)
- [Permanent link](/w/index.php?title=JPEG_File_Interchange_Format&oldid=1364436986)
- [Page information](/w/index.php?title=JPEG_File_Interchange_Format&action=info)
- [Cite this page](/w/index.php?title=Special:CiteThisPage&page=JPEG_File_Interchange_Format&id=1364436986&wpFormIdentifier=titleform)
- [Get shortened URL](/w/index.php?title=Special:UrlShortener&url=https%3A%2F%2Fen.wikipedia.org%2Fwiki%2FJPEG_File_Interchange_Format)
- [Switch to legacy parser](/w/index.php?title=JPEG_File_Interchange_Format&useparsoid=0)

 Print/export

- [Download as PDF](/w/index.php?title=Special:DownloadAsPdf&page=JPEG_File_Interchange_Format&action=show-download-screen)
- [Printable version](/w/index.php?title=JPEG_File_Interchange_Format&printable=yes)

 In other projects

- [Wikidata item](https://www.wikidata.org/wiki/Special:EntityPage/Q26329975)

Appearance
 move to sidebar hide

From Wikipedia, the free encyclopedia

Image file format with multiple editions

The **JPEG File Interchange Format** (**JFIF**) is an [image file format](//en.wikipedia.org/wiki/Image_file_format) standard published as [ITU-T](//en.wikipedia.org/wiki/ITU-T) Recommendation T.871 and [ISO/IEC](//en.wikipedia.org/wiki/ISO/IEC) 10918-5. It defines supplementary specifications for the [container format](//en.wikipedia.org/wiki/Digital_container_format) that contains the image data encoded with the [JPEG](//en.wikipedia.org/wiki/JPEG) algorithm. The base specifications for a JPEG container format are defined in Annex B of the JPEG standard, known as [JPEG Interchange Format](//en.wikipedia.org/wiki/JPEG_Interchange_Format) (JIF). JFIF builds on JIF to solve some of JIF's limitations, including unnecessary complexity, component sample registration, resolution, aspect ratio, and [color space](//en.wikipedia.org/wiki/Color_space). Because JFIF is not the original JPG standard, one might expect another [MIME](//en.wikipedia.org/wiki/MIME) type. However, it is still registered as "image/jpeg" (indicating its primary data format rather than the amended information).

JFIF is [mutually incompatible](#Compatibility) with the newer [Exchangeable image file format](//en.wikipedia.org/wiki/Exchangeable_image_file_format) (Exif).

## Purpose

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=1)]

JFIF defines a number of details that are left unspecified by the JPEG Part 1 standard ([ISO](//en.wikipedia.org/wiki/ISO)/[IEC](//en.wikipedia.org/wiki/International_Electrotechnical_Commission) 10918-1, [ITU-T](//en.wikipedia.org/wiki/ITU-T) Recommendation T.81.)[[1]](#cite_note-itu_t81-1)

### Component sample registration

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=2)]

JPEG allows multiple components (such as [Y, Cb, and Cr](//en.wikipedia.org/wiki/YCbCr)) to have different resolutions, but it does not define how those differing sample arrays (which render bitmaps) should be aligned. This pixel-producing information is rendered with the expectation of indicating rectangles by their [centroid](//en.wikipedia.org/wiki/Centroid), rather than being pixel data directly, or being 'first corner and flood', etc. which is uncommon.

### Resolution and aspect ratio

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=3)]

The JPEG standard does not include any method of coding the resolution or aspect ratio of an image. JFIF provides resolution or aspect ratio information using an application segment extension to JPEG. It uses Application Segment #0, with a segment header consisting of the [null-terminated string](//en.wikipedia.org/wiki/Null-terminated_string) spelling "JFIF" in [ASCII](//en.wikipedia.org/wiki/ASCII) followed by a byte equal to 0, and specifies that this must be the first segment in the file, hence making it simple to recognize a JFIF file. [Exif](//en.wikipedia.org/wiki/Exif) images recorded by digital cameras generally do not include this segment, but typically comply in all other respects with the JFIF standard.

### Color space

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=4)]

The JPEG standard used for the compression coding in JFIF files does not define which [color encoding](//en.wikipedia.org/wiki/Color_space) is to be used for images. JFIF defines the [color model](//en.wikipedia.org/wiki/Color_model) to be used: either Y for greyscale, or [YCbCr](//en.wikipedia.org/wiki/YCbCr) derived from [RGB color primaries](//en.wikipedia.org/wiki/RGB_color_model) as defined in [CCIR 601](//en.wikipedia.org/wiki/CCIR_601) (now known as Rec. [ITU-R](//en.wikipedia.org/wiki/ITU-R) BT.601), except with a different "full range" scaling of the Y, Cb and Cr components. Unlike the "studio range" defined in CCIR 601, in which black is represented by Y=16 and white by Y=235 and values outside of this range are available for [signal processing](//en.wikipedia.org/wiki/Signal_processing) "headroom" and "footroom", JFIF uses all 256 levels of the 8-bit representation, so that Y=0 for black and Y=255 for peak white. The RGB color primaries defined in JFIF via CCIR 601 also differ somewhat from what has become common practice in newer applications (e.g., they differ slightly from the color primaries defined in [sRGB](//en.wikipedia.org/wiki/SRGB)). Moreover, CCIR 601 (before 2007) did not provide a precise definition of the RGB color primaries; it relied instead on the underlying practices of the television industry.

Color interpretation of a JFIF image may be improved by embedding an [ICC](//en.wikipedia.org/wiki/International_Color_Consortium) profile, colorspace metadata, or an [sRGB](//en.wikipedia.org/wiki/SRGB) tag, and using an application that interprets this information.

## File format structure

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=5)]

A JFIF file consists of a sequence of markers or marker segments (for details refer to [JPEG, Syntax and structure](//en.wikipedia.org/wiki/JPEG#Syntax_and_structure)). The markers are defined in part 1 of the [JPEG](//en.wikipedia.org/wiki/JPEG) Standard.[[1]](#cite_note-itu_t81-1) Each marker consists of two bytes: an `FF` byte followed by a byte which is not equal to `00` or `FF` and specifies the type of the marker. Some markers stand alone, but most indicate the start of a marker segment that contains data bytes according to the following pattern:

`FF *xx* *s1* *s2* *[data bytes]*`

The bytes *s1* and *s2* are taken together to represent a [big-endian](//en.wikipedia.org/wiki/Endianness) 16-bit integer specifying the length of the following "data bytes" plus the 2 bytes used to represent the length. In other words, *s1* and *s2* specify the number of the following *data bytes* as     256 ⋅ s 1 + s 2 − 2   {\displaystyle 256\cdot s1+s2-2}  .

According to part 1 of the JPEG standard, applications can use APP marker segments and define an application specific meaning of the data. In the JFIF standard, the following APP marker segments are defined:

- JFIF APP0 marker segment (JFIF segment for short) (mandatory)

- JFIF extension APP0 marker segment (JFXX segment for short) (optional)

They are described below.

The JFIF standard requires that the JFIF APP0 marker segment immediately follows the SOI marker. If a JFIF extension APP0 marker segment is used, it must immediately follow the JFIF APP0 marker segment.[[2]](#cite_note-hamilton_1992-2) So a JFIF file will have the following structure:

| JFIF file structure                                        |                                |                     |
|------------------------------------------------------------|--------------------------------|---------------------|
| Segment                                                    | Code                           | Description         |
| SOI                                                        | FF D8                          | Start of Image      |
| JFIF-APP0                                                  | FF E0 s1 s2 4A 46 49 46 00 ... | see below           |
| JFXX-APP0                                                  | FF E0 s1 s2 4A 46 58 58 00 ... | optional, see below |
| … additional marker segments (for example SOF, DHT, COM) |                                |                     |
| SOS                                                        | FF DA                          | Start of Scan       |
|                                                            | compressed image data          |                     |
| EOI                                                        | FF D9                          | End of Image        |

### JFIF APP0 marker segment

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=6)]

In the mandatory JFIF APP0 marker segment the parameters of the image are specified. Optionally an uncompressed thumbnail can be embedded.

| JFIF APP0 marker segment |              |                                                                                                                                                                               |
|--------------------------|--------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Field                    | Size (bytes) | Description                                                                                                                                                                   |
| APP0 marker              | 2            | FF E0                                                                                                                                                                         |
| Length                   | 2            | Length of segment excluding APP0 marker                                                                                                                                       |
| Identifier               | 5            | 4A 46 49 46 00 = "JFIF" in ASCII, terminated by a null byte                                                                                                                   |
| JFIF version             | 2            | First byte for major version, second byte for minor version (01 02 for 1.02)                                                                                                  |
| Density units            | 1            | Units for the following pixel density fields 00 : No units; width:height pixel aspect ratio = Ydensity:Xdensity 01 : Pixels per inch (2.54 cm) 02 : Pixels per centimeter |
| Xdensity                 | 2            | Horizontal pixel density. Must not be zero                                                                                                                                    |
| Ydensity                 | 2            | Vertical pixel density. Must not be zero                                                                                                                                      |
| Xthumbnail               | 1            | Horizontal pixel count of the following embedded RGB thumbnail. May be zero                                                                                                   |
| Ythumbnail               | 1            | Vertical pixel count of the following embedded RGB thumbnail. May be zero                                                                                                     |
| Thumbnail data           | 3 × n       | Uncompressed 24 bit RGB (8 bits per color channel) raster thumbnail data in the order R0, G0, B0, ... Rn-1, Gn-1, Bn-1; with n = Xthumbnail × Ythumbnail                     |

### JFIF extension APP0 marker segment

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=7)]

Immediately following the JFIF APP0 marker segment may be a JFIF extension APP0 marker segment. This segment may only be present for JFIF versions 1.02 and above. It allows to embed a thumbnail image in 3 different formats.

| JFIF extension APP0 marker segment |              |                                                                                                                                                                       |
|------------------------------------|--------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Field                              | Size (bytes) | Description                                                                                                                                                           |
| APP0 marker                        | 2            | FF E0                                                                                                                                                                 |
| Length                             | 2            | Length of segment excluding APP0 marker                                                                                                                               |
| Identifier                         | 5            | 4A 46 58 58 00 = "JFXX" in ASCII, terminated by a null byte                                                                                                           |
| Thumbnail format                   | 1            | Specifies what data format is used for the following embedded thumbnail: 10 : JPEG format 11 : 1 byte per pixel palettized format 13 : 3 byte per pixel RGB format |
| Thumbnail data                     | variable     | Depends on the thumbnail format, see below                                                                                                                            |

The thumbnail data depends on the thumbnail format as follows:

| Thumbnail stored using JPEG encoding |              |                                                                                      |
|--------------------------------------|--------------|--------------------------------------------------------------------------------------|
| Field                                | Size (bytes) | Description                                                                          |
| SOI                                  | 2            | FF D8                                                                                |
|                                      | variable     | Must be JIF format using YCbCr or just Y, and must not contain JFIF or JFXX segments |
| EOI                                  | 2            | FF D9                                                                                |

| Thumbnail stored using one byte per pixel |              |                                                                                                            |
|-------------------------------------------|--------------|------------------------------------------------------------------------------------------------------------|
| Field                                     | Size (bytes) | Description                                                                                                |
| Xthumbnail                                | 1            | Horizontal pixel count of the following embedded thumbnail. Must not be zero                               |
| Ythumbnail                                | 1            | Vertical pixel count of the following embedded thumbnail. Must not be zero                                 |
| Thumbnail palette                         | 768          | 256 palette entries, each containing a 24 bit RGB color value                                              |
| Thumbnail data                            | n            | One byte per pixel containing the index of the color within the palette, with n = Xthumbnail × Ythumbnail |

| Thumbnail stored using three byte per pixel |              |                                                                                                                                                           |
|---------------------------------------------|--------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------|
| Field                                       | Size (bytes) | Description                                                                                                                                               |
| Xthumbnail                                  | 1            | Horizontal pixel count of the following embedded thumbnail. Must not be zero                                                                              |
| Ythumbnail                                  | 1            | Vertical pixel count of the following embedded thumbnail. Must not be zero                                                                                |
| Thumbnail data                              | 3 × n       | Uncompressed 24 bit RGB (8 bits per color channel) raster thumbnail data in the order R0, G0, B0, ... Rn-1, Gn-1, Bn-1; with n = Xthumbnail × Ythumbnail |

## Compatibility

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=8)]

The newer [Exchangeable image file format](//en.wikipedia.org/wiki/Exchangeable_image_file_format) (Exif) is comparable to JFIF, but the two standards are mutually incompatible. This is because both standards specify that their particular application segment (APP0 for JFIF, APP1 for Exif) must immediately follow the SOI marker. In practice, many programs and digital cameras produce files with both application segments included. This will not affect the image decoding for most decoders, but poorly designed JFIF or Exif parsers may not recognise the file properly.

JFIF is compatible with Adobe [Photoshop](//en.wikipedia.org/wiki/Photoshop)'s JPEG "Information Resource Block" extensions, and [IPTC Information Interchange Model](//en.wikipedia.org/wiki/IPTC_Information_Interchange_Model) metadata, since JFIF does not preclude other application segments, and the Photoshop extensions are not required to be the first in the file. However, Photoshop generally saves CMYK buffers as four-component "Adobe JPEGs" that are not conformant with JFIF. Since these files are not in a YCbCr color space, they are typically not decodable by Web browsers and other Internet software.

## History

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=9)]

Development of the JFIF document was led by Eric Hamilton of [C-Cube Microsystems](//en.wikipedia.org/wiki/C-Cube_Microsystems), and agreement on the first version was established in late 1991 at a meeting held at C-Cube involving about 40 representatives of various computer, telecommunications, and imaging companies. Shortly afterwards, a minor revision was published — JFIF 1.01.[[3]](#cite_note-ecma_tr98-3) For nearly 20 years, the latest version available was v1.02, published September 1, 1992.[[2]](#cite_note-hamilton_1992-2)

In 1996, [RFC](//en.wikipedia.org/wiki/Request_for_Comments) 2046 specified that the image format used for transmitting JPEG images across the Internet should be JFIF. The [MIME type](//en.wikipedia.org/wiki/MIME_type) of "image/jpeg" must be encoded as JFIF. In practice, however, virtually all Internet software can decode any baseline *JIF* image that uses Y or YCbCr components, whether it is JFIF compliant or not.

As time went by, C-Cube was restructured (and eventually devolved into [Harmonic](//en.wikipedia.org/wiki/Harmonic_Inc.), [LSI Logic](//en.wikipedia.org/wiki/LSI_Logic), [Magnum Semiconductor](//en.wikipedia.org/wiki/Magnum_Semiconductor), [Avago Technologies](//en.wikipedia.org/wiki/Avago_Technologies), [Broadcom](//en.wikipedia.org/wiki/Broadcom_Limited), and GigOptix, GigPeak, etc), and lost interest in the document, and the specification had no official publisher until it was picked up by [Ecma International](//en.wikipedia.org/wiki/Ecma_International) and the ITU-T/ISO/IEC [Joint Photographic Experts Group](//en.wikipedia.org/wiki/Joint_Photographic_Experts_Group) around 2009 to avoid it being lost to history and provide a way to formally cite it in standard publications and improve its editorial quality. It was published by ECMA in 2009 as Technical Report number 98 to avoid loss of the historical record,[[3]](#cite_note-ecma_tr98-3) and it was formally standardized by [ITU-T](//en.wikipedia.org/wiki/ITU-T) in 2011 as its Recommendation T.871[[4]](#cite_note-itu_t871-4) and by ISO/IEC in 2013 as ISO/IEC 10918-5,[[5]](#cite_note-iso_10918-5-5) The newer publications included editorial improvements but no substantial technical changes.

## See also

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=10)]

- [Joint Photographic Experts Group](//en.wikipedia.org/wiki/Joint_Photographic_Experts_Group)

## References

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=11)]

1. [1](#cite_ref-itu_t81_1-0) [2](#cite_ref-itu_t81_1-1) ["Recommendation ITU-T T.81: Information technology – Digital compression and coding of continuous-tone still images – Requirements and guidelines"](http://www.itu.int/rec/T-REC-T.81) (PDF). *ITU-T (formerly CCITT)*. 18 February 1992. Retrieved 15 June 2015.

2. [1](#cite_ref-hamilton_1992_2-0) [2](#cite_ref-hamilton_1992_2-1) Hamilton, Eric (12 September 1992). ["JPEG File Interchange Format, Version 1.02"](http://www.w3.org/Graphics/JPEG/jfif3.pdf) (pdf, 0.02 MB). Retrieved 15 June 2015.

3. [1](#cite_ref-ecma_tr98_3-0) [2](#cite_ref-ecma_tr98_3-1) ["JPEG File Interchange Format (JFIF)"](https://ecma-international.org/publications-and-standards/technical-reports/ecma-tr-98/). *ecma-international.org*. 2009. Retrieved 15 June 2015.

4. [↑](#cite_ref-itu_t871_4-0) ["Recommendation ITU-T T.871: Information technology – Digital compression and coding of continuous-tone still images: JPEG File Interchange Format (JFIF)"](https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-T.871-201105-I!!PDF-E&type=items) (PDF). ITU-T. 14 May 2011. Retrieved 15 June 2015.

5. [↑](#cite_ref-iso_10918-5_5-0) ["ISO/IEC 10918-5:2013: Information technology – Digital compression and coding of continuous-tone still images: JPEG File Interchange Format (JFIF)"](http://www.iso.org/iso/iso_catalogue/catalogue_tc/catalogue_detail.htm?csnumber=54989). ISO/International Electrotechnical Commission. 1 May 2013. Retrieved 15 June 2015.

## Further reading

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=12)]

### Books

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=13)]

- Miano, John M, "Compressed Image File Formats"; 1999, Addison-Wesley [ISBN](//en.wikipedia.org/wiki/ISBN_(identifier)) [978-0-201-60443-6](//en.wikipedia.org/wiki/Special:BookSources/978-0-201-60443-6)

- Pennebaker, William B. and [Joan L. Mitchell](//en.wikipedia.org/wiki/Joan_L._Mitchell): *JPEG still image data compression standard*; 3rd edition, 1993, Springer [ISBN](//en.wikipedia.org/wiki/ISBN_(identifier)) [978-0-442-01272-4](//en.wikipedia.org/wiki/Special:BookSources/978-0-442-01272-4)

### Standards

[[edit](/w/index.php?title=JPEG_File_Interchange_Format&action=edit&section=14)]

- Hamilton, Eric: [*JPEG File Interchange Format, Version 1.02*](http://www.w3.org/Graphics/JPEG/jfif3.pdf) (PDF, 0.02 MB) 1 September 1992

- Recommendation ITU-T T.871: [*Information technology – Digital compression and coding of continuous-tone still images: JPEG File Interchange Format (JFIF)*](https://www.itu.int/rec/T-REC-T.871) (PDF and Microsoft Word, 0.2 MB) Approved 14 May 2011; posted 11 September 2012

- Recommendation ITU-T T.81: [*Information technology – Digital compression and coding of continuous-tone still images – Requirements and guidelines*](http://www.itu.int/rec/T-REC-T.81) (PDF and Microsoft Word, 1.5 MB) Approved 18 September 1992; posted 14 April 2004

Retrieved from "[https://en.wikipedia.org/w/index.php?title=JPEG_File_Interchange_Format&oldid=1364436986](https://en.wikipedia.org/w/index.php?title=JPEG_File_Interchange_Format&oldid=1364436986)"

[Categories](/wiki/Help:Category):

- [JPEG](/wiki/Category:JPEG)
- [Graphics file formats](/wiki/Category:Graphics_file_formats)
- [Open formats](/wiki/Category:Open_formats)

Hidden categories:

- [Articles with short description](/wiki/Category:Articles_with_short_description)
- [Short description is different from Wikidata](/wiki/Category:Short_description_is_different_from_Wikidata)

-  This page was last edited on 16 July 2026, at 14:32 (UTC).

- Page was rendered with [Parsoid](https://www.mediawiki.org/wiki/Special:MyLanguage/Parsoid).

- Text is available under the [Creative Commons Attribution-ShareAlike 4.0 License](/wiki/Wikipedia:Text_of_the_Creative_Commons_Attribution-ShareAlike_4.0_International_License); additional terms may apply. By using this site, you agree to the [Terms of Use](https://foundation.wikimedia.org/wiki/Special:MyLanguage/Policy:Terms_of_Use) and [Privacy Policy](https://foundation.wikimedia.org/wiki/Special:MyLanguage/Policy:Privacy_policy). Wikipedia® is a registered trademark of the [Wikimedia Foundation, Inc.](https://wikimediafoundation.org/), a non-profit organization.

- [Privacy policy](https://foundation.wikimedia.org/wiki/Special:MyLanguage/Policy:Privacy_policy)

- [About Wikipedia](/wiki/Wikipedia:About)

- [Disclaimers](/wiki/Wikipedia:General_disclaimer)

- [Contact Wikipedia](//en.wikipedia.org/wiki/Wikipedia:Contact_us)

- [Legal & safety contacts](https://foundation.wikimedia.org/wiki/Special:MyLanguage/Legal:Wikimedia_Foundation_Legal_and_Safety_Contact_Information)

- [Code of Conduct](https://foundation.wikimedia.org/wiki/Special:MyLanguage/Policy:Universal_Code_of_Conduct)

- [Developers](https://developer.wikimedia.org)

- [Statistics](https://stats.wikimedia.org/#/en.wikipedia.org)

- [Cookie statement](https://foundation.wikimedia.org/wiki/Special:MyLanguage/Policy:Cookie_statement)

- [Mobile view](//en.wikipedia.org/w/index.php?title=JPEG_File_Interchange_Format&mobileaction=toggle_view_mobile)

- [](https://www.wikimedia.org/)

- [](https://www.mediawiki.org/)

  Search

 Search

   Toggle the table of contents

JPEG File Interchange Format

 [](#) [](#) [](#) [](#) [](#) [](#) [](#)

  8 languages  [Add topic](#)