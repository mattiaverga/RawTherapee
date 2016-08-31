/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "iccstore.h"

#include <cstring>

#ifdef WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#include <glib/gstdio.h>

#include "iccmatrices.h"

#include "../rtgui/options.h"

namespace rtengine
{
extern const Settings* settings;

void loadProfiles (const Glib::ustring& dirName,
                   std::map<Glib::ustring, cmsHPROFILE>* profiles,
                   std::map<Glib::ustring, ProfileContent>* profileContents,
                   std::map<Glib::ustring, Glib::ustring>* profileNames,
                   bool nameUpper, bool onlyRgb)
{
    if (dirName.empty ()) {
        return;
    }

    try {

        Glib::Dir dir (dirName);

        for (Glib::DirIterator entry = dir.begin (); entry != dir.end (); ++entry) {

            const Glib::ustring fileName = *entry;

            if (fileName.size () < 4) {
                continue;
            }

            const Glib::ustring extension = fileName.substr (fileName.size () - 4).casefold ();

            if (extension.compare (".icc") != 0 && extension.compare (".icm") != 0) {
                continue;
            }

            const Glib::ustring filePath = Glib::build_filename (dirName, fileName);

            if (!Glib::file_test (filePath, Glib::FILE_TEST_IS_REGULAR)) {
                continue;
            }

            Glib::ustring name = fileName.substr (0, fileName.size() - 4);

            if (nameUpper) {
                name = name.uppercase ();
            }

            if (profiles) {
                const ProfileContent content (filePath);
                const cmsHPROFILE profile = content.toProfile ();

                if (profile && (!onlyRgb || cmsGetColorSpace (profile) == cmsSigRgbData)) {
                    profiles->insert (std::make_pair (name, profile));

                    if (profileContents) {
                        profileContents->insert (std::make_pair (name, content));
                    }
                }
            }

            if (profileNames) {
                profileNames->insert (std::make_pair (name, filePath));
            }
        }
    } catch (Glib::Exception&) {}
}

inline void getSupportedIntent (cmsHPROFILE profile, cmsUInt32Number intent, cmsUInt32Number direction, uint8_t& result)
{
    if (cmsIsIntentSupported (profile, intent, direction)) {
        result |= 1 << intent;
    }
}

inline uint8_t getSupportedIntents (cmsHPROFILE profile, cmsUInt32Number direction)
{
    if (!profile) {
        return 0;
    }

    uint8_t result = 0;

    getSupportedIntent (profile, INTENT_PERCEPTUAL, direction, result);
    getSupportedIntent (profile, INTENT_RELATIVE_COLORIMETRIC, direction, result);
    getSupportedIntent (profile, INTENT_SATURATION, direction, result);
    getSupportedIntent (profile, INTENT_ABSOLUTE_COLORIMETRIC, direction, result);

    return result;
}

inline cmsHPROFILE createXYZProfile ()
{
    double mat[3][3] = { {1.0, 0, 0}, {0, 1.0, 0}, {0, 0, 1.0} };
    return ICCStore::createFromMatrix (mat, false, "XYZ");
}

const double (*wprofiles[])[3]  = {xyz_sRGB, xyz_adobe, xyz_prophoto, xyz_widegamut, xyz_bruce, xyz_beta, xyz_best, xyz_rec2020};
const double (*iwprofiles[])[3] = {sRGB_xyz, adobe_xyz, prophoto_xyz, widegamut_xyz, bruce_xyz, beta_xyz, best_xyz, rec2020_xyz};
const char* wpnames[] = {"sRGB", "Adobe RGB", "ProPhoto", "WideGamut", "BruceRGB", "Beta RGB", "BestRGB", "Rec2020"};
const char* wpgamma[] = {"default", "BT709_g2.2_s4.5", "sRGB_g2.4_s12.92", "linear_g1.0", "standard_g2.2", "standard_g1.8", "High_g1.3_s3.35", "Low_g2.6_s6.9"}; //gamma free
//default = gamma inside profile
//BT709 g=2.22 s=4.5  sRGB g=2.4 s=12.92
//linear g=1.0
//std22 g=2.2   std18 g=1.8
// high  g=1.3 s=3.35  for high dynamic images
//low  g=2.6 s=6.9  for low contrast images

}

namespace rtengine
{

std::vector<Glib::ustring> getGamma ()
{

    std::vector<Glib::ustring> res;

    for (unsigned int i = 0; i < sizeof(wpgamma) / sizeof(wpgamma[0]); i++) {
        res.push_back (wpgamma[i]);
    }

    return res;
}

std::vector<Glib::ustring> getWorkingProfiles ()
{

    std::vector<Glib::ustring> res;

    for (unsigned int i = 0; i < sizeof(wpnames) / sizeof(wpnames[0]); i++) {
        res.push_back (wpnames[i]);
    }

    return res;
}

std::vector<Glib::ustring> ICCStore::getProfiles () const
{

    MyMutex::MyLock lock(mutex_);

    std::vector<Glib::ustring> res;

    for (ProfileMap::const_iterator profile = fileProfiles.begin (); profile != fileProfiles.end (); ++profile) {
        res.push_back (profile->first);
    }

    return res;
}

std::vector<Glib::ustring> ICCStore::getProfilesFromDir (const Glib::ustring& dirName) const
{

    MyMutex::MyLock lock(mutex_);

    std::vector<Glib::ustring> res;

    ProfileMap profiles;

    loadProfiles (profilesDir, &profiles, nullptr, nullptr, false, true);
    loadProfiles (dirName, &profiles, nullptr, nullptr, false, true);

    for (ProfileMap::const_iterator profile = profiles.begin (); profile != profiles.end (); ++profile) {
        res.push_back (profile->first);
    }

    return res;
}

cmsHPROFILE ICCStore::makeStdGammaProfile (cmsHPROFILE iprof)
{
    // forgive me for the messy code, quick hack to change gamma of an ICC profile to the RT standard gamma
    if (!iprof) {
        return nullptr;
    }

    cmsUInt32Number bytesNeeded = 0;
    cmsSaveProfileToMem(iprof, 0, &bytesNeeded);

    if (bytesNeeded == 0) {
        return nullptr;
    }

    uint8_t *data = new uint8_t[bytesNeeded + 1];
    cmsSaveProfileToMem(iprof, data, &bytesNeeded);
    const uint8_t *p = &data[128]; // skip 128 byte header
    uint32_t tag_count;
    memcpy(&tag_count, p, 4);
    p += 4;
    tag_count = ntohl(tag_count);

    struct icctag {
        uint32_t sig;
        uint32_t offset;
        uint32_t size;
    } tags[tag_count];

    const uint32_t gamma = 0x239;
    int gamma_size = (gamma == 0 || gamma == 256) ? 12 : 14;
    int data_size = (gamma_size + 3) & ~3;

    for (uint32_t i = 0; i < tag_count; i++) {
        memcpy(&tags[i], p, 12);
        tags[i].sig = ntohl(tags[i].sig);
        tags[i].offset = ntohl(tags[i].offset);
        tags[i].size = ntohl(tags[i].size);
        p += 12;

        if (tags[i].sig != 0x62545243 && // bTRC
                tags[i].sig != 0x67545243 && // gTRC
                tags[i].sig != 0x72545243 && // rTRC
                tags[i].sig != 0x6B545243) { // kTRC
            data_size += (tags[i].size + 3) & ~3;
        }
    }

    uint32_t sz = 128 + 4 + tag_count * 12 + data_size;
    uint8_t *nd = new uint8_t[sz];
    memset(nd, 0, sz);
    memcpy(nd, data, 128 + 4);
    sz = htonl(sz);
    memcpy(nd, &sz, 4);
    uint32_t offset = 128 + 4 + tag_count * 12;
    uint32_t gamma_offset = 0;

    for (uint32_t i = 0; i < tag_count; i++) {
        struct icctag tag;
        tag.sig = htonl(tags[i].sig);

        if (tags[i].sig == 0x62545243 || // bTRC
                tags[i].sig == 0x67545243 || // gTRC
                tags[i].sig == 0x72545243 || // rTRC
                tags[i].sig == 0x6B545243) { // kTRC
            if (gamma_offset == 0) {
                gamma_offset = offset;
                uint32_t pcurve[] = { htonl(0x63757276), htonl(0), htonl(gamma_size == 12 ? 0U : 1U) };
                memcpy(&nd[offset], pcurve, 12);

                if (gamma_size == 14) {
                    uint16_t gm = htons(gamma);
                    memcpy(&nd[offset + 12], &gm, 2);
                }

                offset += (gamma_size + 3) & ~3;
            }

            tag.offset = htonl(gamma_offset);
            tag.size = htonl(gamma_size);
        } else {
            tag.offset = htonl(offset);
            tag.size = htonl(tags[i].size);
            memcpy(&nd[offset], &data[tags[i].offset], tags[i].size);
            offset += (tags[i].size + 3) & ~3;
        }

        memcpy(&nd[128 + 4 + i * 12], &tag, 12);
    }

    cmsHPROFILE oprof = cmsOpenProfileFromMem (nd, ntohl(sz));
    delete [] nd;
    delete [] data;
    return oprof;
}

ICCStore* ICCStore::getInstance ()
{
    static ICCStore instance_;
    return &instance_;
}

ICCStore::ICCStore () :
    xyz (createXYZProfile ()),
    srgb (cmsCreate_sRGBProfile ())
{
    //cmsErrorAction (LCMS_ERROR_SHOW);

    int N = sizeof(wpnames) / sizeof(wpnames[0]);

    for (int i = 0; i < N; i++) {
        wProfiles[wpnames[i]] = createFromMatrix (wprofiles[i]);
        wProfilesGamma[wpnames[i]] = createFromMatrix (wprofiles[i], true);
        wMatrices[wpnames[i]] = wprofiles[i];
        iwMatrices[wpnames[i]] = iwprofiles[i];
    }
}

TMatrix ICCStore::workingSpaceMatrix (const Glib::ustring& name) const
{

    const MatrixMap::const_iterator r = wMatrices.find (name);

    if (r != wMatrices.end()) {
        return r->second;
    } else {
        return wMatrices.find ("sRGB")->second;
    }
}

TMatrix ICCStore::workingSpaceInverseMatrix (const Glib::ustring& name) const
{

    const MatrixMap::const_iterator r = iwMatrices.find (name);

    if (r != iwMatrices.end()) {
        return r->second;
    } else {
        return iwMatrices.find ("sRGB")->second;
    }
}

cmsHPROFILE ICCStore::workingSpace (const Glib::ustring& name) const
{

    const ProfileMap::const_iterator r = wProfiles.find (name);

    if (r != wProfiles.end()) {
        return r->second;
    } else {
        return wProfiles.find ("sRGB")->second;
    }
}

cmsHPROFILE ICCStore::workingSpaceGamma (const Glib::ustring& name) const
{

    const ProfileMap::const_iterator r = wProfilesGamma.find (name);

    if (r != wProfilesGamma.end()) {
        return r->second;
    } else {
        return wProfilesGamma.find ("sRGB")->second;
    }
}

void ICCStore::getGammaArray(const procparams::ColorManagementParams &icm, GammaValues &ga)
{
    const double eps = 0.000000001; // not divide by zero
    if (!icm.freegamma) {//if Free gamma not selected
        // gamma : ga[0],ga[1],ga[2],ga[3],ga[4],ga[5] by calcul
        if(icm.gamma == "BT709_g2.2_s4.5")      {
            ga[0] = 2.22;    //BT709  2.2  4.5  - my preferred as D.Coffin
            ga[1] = 0.909995;
            ga[2] = 0.090005;
            ga[3] = 0.222222;
            ga[4] = 0.081071;
        } else if (icm.gamma == "sRGB_g2.4_s12.92")   {
            ga[0] = 2.40;    //sRGB 2.4 12.92  - RT default as Lightroom
            ga[1] = 0.947858;
            ga[2] = 0.052142;
            ga[3] = 0.077399;
            ga[4] = 0.039293;
        } else if (icm.gamma == "High_g1.3_s3.35")    {
            ga[0] = 1.3 ;    //for high dynamic images
            ga[1] = 0.998279;
            ga[2] = 0.001721;
            ga[3] = 0.298507;
            ga[4] = 0.005746;
        } else if (icm.gamma == "Low_g2.6_s6.9")   {
            ga[0] = 2.6 ;    //gamma 2.6 variable : for low contrast images
            ga[1] = 0.891161;
            ga[2] = 0.108839;
            ga[3] = 0.144928;
            ga[4] = 0.076332;
        } else if (icm.gamma == "standard_g2.2")   {
            ga[0] = 2.2;    //gamma=2.2 (as gamma of Adobe, Widegamut...)
            ga[1] = 1.;
            ga[2] = 0.;
            ga[3] = 1. / eps;
            ga[4] = 0.;
        } else if (icm.gamma == "standard_g1.8")   {
            ga[0] = 1.8;    //gamma=1.8  (as gamma of Prophoto)
            ga[1] = 1.;
            ga[2] = 0.;
            ga[3] = 1. / eps;
            ga[4] = 0.;
        } else /* if (icm.gamma == "linear_g1.0") */   {
            ga[0] = 1.0;    //gamma=1 linear : for high dynamic images (cf : D.Coffin...)
            ga[1] = 1.;
            ga[2] = 0.;
            ga[3] = 1. / eps;
            ga[4] = 0.;
        }
        ga[5] = 0.0;
        ga[6] = 0.0;
    } else { //free gamma selected
        GammaValues g_a; //gamma parameters
        double pwr = 1.0 / icm.gampos;
        double ts = icm.slpos;
        double slope = icm.slpos == 0 ? eps : icm.slpos;

        int mode = 0, imax = 0;
        Color::calcGamma(pwr, ts, mode, imax, g_a); // call to calcGamma with selected gamma and slope : return parameters for LCMS2
        ga[4] = g_a[3] * ts;
        //printf("g_a.gamma0=%f g_a.gamma1=%f g_a.gamma2=%f g_a.gamma3=%f g_a.gamma4=%f\n", g_a.gamma0,g_a.gamma1,g_a.gamma2,g_a.gamma3,g_a.gamma4);
        ga[0] = icm.gampos;
        ga[1] = 1. / (1.0 + g_a[4]);
        ga[2] = g_a[4] / (1.0 + g_a[4]);
        ga[3] = 1. / slope;
        ga[5] = 0.0;
        ga[6] = 0.0;
        //printf("ga[0]=%f ga[1]=%f ga[2]=%f ga[3]=%f ga[4]=%f\n", ga[0],ga[1],ga[2],ga[3],ga[4]);
    }
}

cmsHPROFILE ICCStore::createGammaProfile (const procparams::ColorManagementParams &icm, GammaValues &ga) {
    float p[6]; //primaries
    ga[6] = 0.0;

    enum class ColorTemp {
        D50 = 5003,  // for Widegamut, Prophoto Best, Beta -> D50
        D65 = 6504   // for sRGB, AdobeRGB, Bruce Rec2020  -> D65
    };
    ColorTemp temp = ColorTemp::D50;

    //primaries for 7 working profiles ==> output profiles
    // eventually to adapt primaries  if RT used special profiles !
    if (icm.output == "WideGamut") {
        p[0] = 0.7350;    //Widegamut primaries
        p[1] = 0.2650;
        p[2] = 0.1150;
        p[3] = 0.8260;
        p[4] = 0.1570;
        p[5] = 0.0180;
    } else if (icm.output == "Adobe RGB") {
        p[0] = 0.6400;    //Adobe primaries
        p[1] = 0.3300;
        p[2] = 0.2100;
        p[3] = 0.7100;
        p[4] = 0.1500;
        p[5] = 0.0600;
        temp = ColorTemp::D65;
    } else if (icm.output == "sRGB") {
        p[0] = 0.6400;    // sRGB primaries
        p[1] = 0.3300;
        p[2] = 0.3000;
        p[3] = 0.6000;
        p[4] = 0.1500;
        p[5] = 0.0600;
        temp = ColorTemp::D65;
    } else if (icm.output == "BruceRGB") {
        p[0] = 0.6400;    // Bruce primaries
        p[1] = 0.3300;
        p[2] = 0.2800;
        p[3] = 0.6500;
        p[4] = 0.1500;
        p[5] = 0.0600;
        temp = ColorTemp::D65;
    } else if (icm.output == "Beta RGB") {
        p[0] = 0.6888;    // Beta primaries
        p[1] = 0.3112;
        p[2] = 0.1986;
        p[3] = 0.7551;
        p[4] = 0.1265;
        p[5] = 0.0352;
    } else if (icm.output == "BestRGB") {
        p[0] = 0.7347;    // Best primaries
        p[1] = 0.2653;
        p[2] = 0.2150;
        p[3] = 0.7750;
        p[4] = 0.1300;
        p[5] = 0.0350;
    } else if (icm.output == "Rec2020") {
        p[0] = 0.7080;    // Rec2020 primaries
        p[1] = 0.2920;
        p[2] = 0.1700;
        p[3] = 0.7970;
        p[4] = 0.1310;
        p[5] = 0.0460;
        temp = ColorTemp::D65;
    } else {
        p[0] = 0.7347;    //ProPhoto and default primaries
        p[1] = 0.2653;
        p[2] = 0.1596;
        p[3] = 0.8404;
        p[4] = 0.0366;
        p[5] = 0.0001;
    }

    cmsCIExyY xyD;
    cmsCIExyYTRIPLE Primaries = {
        {p[0], p[1], 1.0}, // red
        {p[2], p[3], 1.0}, // green
        {p[4], p[5], 1.0}  // blue
    };
    cmsToneCurve* GammaTRC[3];

    // 7 parameters for smoother curves
    cmsFloat64Number Parameters[7] = { ga[0],  ga[1], ga[2], ga[3], ga[4], ga[5], ga[6] } ;

    cmsWhitePointFromTemp(&xyD, (double)temp);
    GammaTRC[0] = GammaTRC[1] = GammaTRC[2] = cmsBuildParametricToneCurve(NULL, 5, Parameters); //5 = smoother than 4
    cmsHPROFILE oprofdef = cmsCreateRGBProfile(&xyD, &Primaries, GammaTRC); //oprofdef  become Outputprofile

    cmsFreeToneCurve(GammaTRC[0]);

    return oprofdef;
}

cmsHPROFILE ICCStore::createCustomGammaOutputProfile (const procparams::ColorManagementParams &icm, GammaValues &ga) {
    bool pro = false;
    Glib::ustring outProfile;
    cmsHPROFILE outputProfile = nullptr;

    if (icm.freegamma && icm.gampos < 1.35) {
        pro = true;    //select profil with gammaTRC modified :
    } else if (icm.gamma == "linear_g1.0" || (icm.gamma == "High_g1.3_s3.35")) {
        pro = true;    //pro=0  RT_sRGB || Prophoto
    }

    // Check that output profiles exist, otherwise use LCMS2
    // Use the icc/icm profiles associated to possible working profiles, set in "options"
    if      (icm.working == "ProPhoto"    && iccStore->outputProfileExist(options.rtSettings.prophoto)   && !pro) {
        outProfile = options.rtSettings.prophoto;
    } else if (icm.working == "Adobe RGB" && iccStore->outputProfileExist(options.rtSettings.adobe)             ) {
        outProfile = options.rtSettings.adobe;
    } else if (icm.working == "WideGamut" && iccStore->outputProfileExist(options.rtSettings.widegamut)         ) {
        outProfile = options.rtSettings.widegamut;
    } else if (icm.working == "Beta RGB"  && iccStore->outputProfileExist(options.rtSettings.beta)              ) {
        outProfile = options.rtSettings.beta;
    } else if (icm.working == "BestRGB"   && iccStore->outputProfileExist(options.rtSettings.best)              ) {
        outProfile = options.rtSettings.best;
    } else if (icm.working == "BruceRGB"  && iccStore->outputProfileExist(options.rtSettings.bruce)             ) {
        outProfile = options.rtSettings.bruce;
    } else if (icm.working == "sRGB"      && iccStore->outputProfileExist(options.rtSettings.srgb)       && !pro) {
        outProfile = options.rtSettings.srgb;
    } else if (icm.working == "sRGB"      && iccStore->outputProfileExist(options.rtSettings.srgb10)     &&  pro) {
        outProfile = options.rtSettings.srgb10;
    } else if (icm.working == "ProPhoto"  && iccStore->outputProfileExist(options.rtSettings.prophoto10) &&  pro) {
        outProfile = options.rtSettings.prophoto10;
    } else if (icm.working == "Rec2020"   && iccStore->outputProfileExist(options.rtSettings.rec2020)           ) {
        outProfile = options.rtSettings.rec2020;
    } else {
        // Should not occurs
        if (settings->verbose) {
            printf("\"%s\": unknown working profile! - use LCMS2 substitution\n", icm.working.c_str() );
        }

        return nullptr;
    }

    //begin adaptation rTRC gTRC bTRC
    //"outputProfile" profile has the same characteristics than RGB values, but TRC are adapted... for applying profile
    if (settings->verbose) {
        printf("Output Gamma - profile: \"%s\"\n", outProfile.c_str()  );    //c_str()
    }

    outputProfile = iccStore->getProfile(outProfile); //get output profile

    if (outputProfile == nullptr) {

        if (settings->verbose) {
            printf("\"%s\" ICC output profile not found!\n", outProfile.c_str());
        }
        return nullptr;
    }

    // 7 parameters for smoother curves
    cmsFloat64Number Parameters[7] = { ga[0], ga[1], ga[2], ga[3], ga[4], ga[5], ga[6] };

    //change desc Tag , to "free gamma", or "BT709", etc.
    cmsMLU *mlu;
    cmsContext ContextID = cmsGetProfileContextID(outputProfile); // create context to modify some TAGs
    mlu = cmsMLUalloc(ContextID, 1);

    // instruction with //ICC are used to generate ICC profile
    if (mlu == nullptr) {
        printf("Description error\n");
    } else {

        // Description TAG : selection of gamma and Primaries
        if (!icm.freegamma) {
            std::wstring gammaStr;

            if(icm.gamma == "High_g1.3_s3.35") {
                gammaStr = std::wstring(L"GammaTRC: High g=1.3 s=3.35");
            } else if (icm.gamma == "Low_g2.6_s6.9") {
                gammaStr = std::wstring(L"GammaTRC: Low g=2.6 s=6.9");
            } else if (icm.gamma == "sRGB_g2.4_s12.92") {
                gammaStr = std::wstring(L"GammaTRC: sRGB g=2.4 s=12.92");
            } else if (icm.gamma == "BT709_g2.2_s4.5") {
                gammaStr = std::wstring(L"GammaTRC: BT709 g=2.2 s=4.5");
            } else if (icm.gamma == "linear_g1.0") {
                gammaStr = std::wstring(L"GammaTRC: Linear g=1.0");
            } else if (icm.gamma == "standard_g2.2") {
                gammaStr = std::wstring(L"GammaTRC: g=2.2");
            } else if (icm.gamma == "standard_g1.8") {
                gammaStr = std::wstring(L"GammaTRC: g=1.8");
            }

            cmsMLUsetWide(mlu,  "en", "US", gammaStr.c_str());
        } else {
            // create description with gamma + slope + primaries
            std::wostringstream gammaWs;
            gammaWs.precision(2);
            gammaWs << "Manual GammaTRC: g=" << (float)icm.gampos << " s=" << (float)icm.slpos;

            cmsMLUsetWide(mlu,  "en", "US", gammaWs.str().c_str());
        }

        cmsWriteTag(outputProfile, cmsSigProfileDescriptionTag,  mlu);//desc changed

        /*
        cmsMLUsetWide(mlu, "en", "US", L"General Public License - AdobeRGB compatible");//adapt to profil
        cmsWriteTag(outputProfile, cmsSigCopyrightTag, mlu);

        cmsMLUsetWide(mlu, "en", "US", L"RawTherapee");
        cmsWriteTag(outputProfile, cmsSigDeviceMfgDescTag, mlu);

        cmsMLUsetWide(mlu, "en", "US", L"RTMedium");   //adapt to profil
        cmsWriteTag(outputProfile, cmsSigDeviceModelDescTag, mlu);

        */

        cmsMLUfree (mlu);
    }

    // Calculate output profile's rTRC gTRC bTRC
    cmsToneCurve* GammaTRC = nullptr;
    GammaTRC = cmsBuildParametricToneCurve(nullptr, 5, Parameters);
    cmsWriteTag(outputProfile, cmsSigRedTRCTag,   (void*)GammaTRC );
    cmsWriteTag(outputProfile, cmsSigGreenTRCTag, (void*)GammaTRC );
    cmsWriteTag(outputProfile, cmsSigBlueTRCTag,  (void*)GammaTRC );

    if (GammaTRC) {
        cmsFreeToneCurve(GammaTRC);
    }

    return outputProfile;
}

bool ICCStore::outputProfileExist (const Glib::ustring& name) const
{

    MyMutex::MyLock lock(mutex_);
    return fileProfiles.find(name) != fileProfiles.end();
}

cmsHPROFILE ICCStore::getProfile (const Glib::ustring& name) const
{

    MyMutex::MyLock lock(mutex_);

    const ProfileMap::const_iterator r = fileProfiles.find (name);

    if (r != fileProfiles.end ()) {
        return r->second;
    }

    if (name.compare (0, 5, "file:") == 0) {
        const ProfileContent content (name.substr (5));
        const cmsHPROFILE profile = content.toProfile ();

        if (profile) {
            const_cast<ProfileMap&>(fileProfiles).insert(std::make_pair(name, profile));
            const_cast<ContentMap&>(fileProfileContents).insert(std::make_pair(name, content));

            return profile;
        }
    }

    return nullptr;
}

cmsHPROFILE ICCStore::getStdProfile (const Glib::ustring& name) const
{

    const Glib::ustring nameUpper = name.uppercase ();

    MyMutex::MyLock lock(mutex_);

    const ProfileMap::const_iterator r = fileStdProfiles.find (nameUpper);

    // return profile from store
    if (r != fileStdProfiles.end ()) {
        return r->second;
    }

    // profile is not yet in store
    const NameMap::const_iterator f = fileStdProfilesFileNames.find (nameUpper);

    // profile does not exist
    if (f == fileStdProfilesFileNames.end ()) {
        return nullptr;
    }

    // but there exists one => load it
    const ProfileContent content (f->second);
    const cmsHPROFILE profile = content.toProfile ();

    if (profile) {
        const_cast<ProfileMap&>(fileStdProfiles).insert (std::make_pair (f->first, profile));
    }

    // profile is not valid or it is now stored => remove entry from fileStdProfilesFileNames
    const_cast<NameMap&>(fileStdProfilesFileNames).erase (f);
    return profile;
}

ProfileContent ICCStore::getContent (const Glib::ustring& name) const
{

    MyMutex::MyLock lock(mutex_);

    const ContentMap::const_iterator r = fileProfileContents.find (name);

    return r != fileProfileContents.end () ? r->second : ProfileContent();
}

uint8_t ICCStore::getInputIntents (cmsHPROFILE profile) const
{
    MyMutex::MyLock lock (mutex_);

    return getSupportedIntents (profile, LCMS_USED_AS_INPUT);
}

uint8_t ICCStore::getOutputIntents (cmsHPROFILE profile) const
{
    MyMutex::MyLock lock (mutex_);

    return getSupportedIntents (profile, LCMS_USED_AS_OUTPUT);
}

uint8_t ICCStore::getProofIntents (cmsHPROFILE profile) const
{
    MyMutex::MyLock lock (mutex_);

    return getSupportedIntents (profile, LCMS_USED_AS_PROOF);
}

// Reads all profiles from the given profiles dir
void ICCStore::init (const Glib::ustring& usrICCDir, const Glib::ustring& rtICCDir)
{

    MyMutex::MyLock lock(mutex_);

    // RawTherapee's profiles take precedence if a user's profile of the same name exists
    profilesDir = Glib::build_filename (rtICCDir, "output");
    fileProfiles.clear();
    fileProfileContents.clear();
    loadProfiles (profilesDir, &fileProfiles, &fileProfileContents, nullptr, false, true);
    loadProfiles (usrICCDir, &fileProfiles, &fileProfileContents, nullptr, false, true);

    // Input profiles
    // Load these to different areas, since the short name (e.g. "NIKON D700" may overlap between system/user and RT dir)
    stdProfilesDir = Glib::build_filename (rtICCDir, "input");
    fileStdProfiles.clear();
    fileStdProfilesFileNames.clear();
    loadProfiles (stdProfilesDir, nullptr, nullptr, &fileStdProfilesFileNames, true, false);
}

// Determine the first monitor default profile of operating system, if selected
void ICCStore::findDefaultMonitorProfile ()
{
    defaultMonitorProfile.clear ();

#ifdef WIN32
    // Get current main monitor. Could be fine tuned to get the current windows monitor (multi monitor setup),
    // but problem is that we live in RTEngine with no GUI window to query around
    HDC hDC = GetDC(NULL);

    if (hDC != NULL) {
        if (SetICMMode(hDC, ICM_ON)) {
            char profileName[MAX_PATH + 1];
            DWORD profileLength = MAX_PATH;

            if (GetICMProfileA(hDC, &profileLength, profileName)) {
                defaultMonitorProfile = Glib::ustring(profileName);
                defaultMonitorProfile = Glib::path_get_basename(defaultMonitorProfile);
                size_t pos = defaultMonitorProfile.rfind(".");

                if (pos != Glib::ustring::npos) {
                    defaultMonitorProfile = defaultMonitorProfile.substr(0, pos);
                }
            }

            // might fail if e.g. the monitor has no profile
        }

        ReleaseDC(NULL, hDC);
    }

#else
// TODO: Add other OS specific code here
#endif

    if (options.rtSettings.verbose) {
        printf("Default monitor profile is: %s\n", defaultMonitorProfile.c_str());
    }
}

ProfileContent::ProfileContent (const Glib::ustring& fileName) : data(nullptr), length(0)
{

    FILE* f = g_fopen (fileName.c_str (), "rb");

    if (!f) {
        return;
    }

    fseek (f, 0, SEEK_END);
    length = ftell (f);
    fseek (f, 0, SEEK_SET);
    data = new char[length + 1];
    fread (data, length, 1, f);
    data[length] = 0;
    fclose (f);
}

ProfileContent::ProfileContent (const ProfileContent& other)
{

    length = other.length;

    if (other.data) {
        data = new char[length + 1];
        memcpy (data, other.data, length + 1);
    } else {
        data = nullptr;
    }
}

ProfileContent::ProfileContent (cmsHPROFILE hProfile) : data(nullptr), length(0)
{

    if (hProfile != nullptr) {
        cmsUInt32Number bytesNeeded = 0;
        cmsSaveProfileToMem(hProfile, 0, &bytesNeeded);

        if (bytesNeeded > 0) {
            data = new char[bytesNeeded + 1];
            cmsSaveProfileToMem(hProfile, data, &bytesNeeded);
            length = (int)bytesNeeded;
        }
    }
}


ProfileContent& ProfileContent::operator= (const ProfileContent& other)
{

    length = other.length;

    delete [] data;

    if (other.data) {
        data = new char[length + 1];
        memcpy (data, other.data, length + 1);
    } else {
        data = nullptr;
    }

    return *this;
}

cmsHPROFILE ProfileContent::toProfile () const
{

    if (data) {
        return cmsOpenProfileFromMem (data, length);
    } else {
        return nullptr;
    }
}

cmsHPROFILE ICCStore::createFromMatrix (const double matrix[3][3], bool gamma, const Glib::ustring& name)
{

    static const unsigned phead[] = {
        1024, 0, 0x2100000, 0x6d6e7472, 0x52474220, 0x58595a20, 0, 0, 0,
        0x61637370, 0, 0, 0, 0, 0, 0, 0, 0xf6d6, 0x10000, 0xd32d
    };
    unsigned pbody[] = {
        10, 0x63707274, 0, 36,  /* cprt */
        0x64657363, 0, 40,  /* desc */
        0x77747074, 0, 20,  /* wtpt */
        0x626b7074, 0, 20,  /* bkpt */
        0x72545243, 0, 14,  /* rTRC */
        0x67545243, 0, 14,  /* gTRC */
        0x62545243, 0, 14,  /* bTRC */
        0x7258595a, 0, 20,  /* rXYZ */
        0x6758595a, 0, 20,  /* gXYZ */
        0x6258595a, 0, 20
    };    /* bXYZ */
    static const unsigned pwhite[] = { 0xf351, 0x10000, 0x116cc };//D65
    //static const unsigned pwhite[] = { 0xf6d6, 0x10000, 0xd340 };//D50

    // 0x63757276 : curveType, 0 : reserved, 1 : entries (1=gamma, 0=identity), 0x1000000=1.0
    unsigned pcurve[] = { 0x63757276, 0, 0, 0x1000000 };
//    unsigned pcurve[] = { 0x63757276, 0, 1, 0x1000000 };

    if (gamma) {
        pcurve[2] = 1;
        // pcurve[3] = 0x1f00000;// pcurve for gamma BT709 : g=2.22 s=4.5
        // normalize gamma in RT, default (Emil's choice = sRGB)
        pcurve[3] = 0x2390000;//pcurve for gamma sRGB : g:2.4 s=12.92

    } else {
        // lcms2 up to 2.4 has a bug with linear gamma causing precision loss (banding)
        // of floating point data when a normal icc encoding of linear gamma is used
        // (i e 0 table entries), but by encoding a gamma curve which is 1.0 the
        // floating point path is taken within lcms2 so no precision loss occurs and
        // gamma is still 1.0.
        pcurve[2] = 1;
        pcurve[3] = 0x1000000; //pcurve for gamma 1
    }

    // constructing profile header
    unsigned* oprof = new unsigned [phead[0] / sizeof(unsigned)];
    memset (oprof, 0, phead[0]);
    memcpy (oprof, phead, sizeof(phead));

    oprof[0] = 132 + 12 * pbody[0];

    // constructing tag directory (pointers inside the file), and types
    // 0x74657874 : text
    // 0x64657363 : description tag
    for (unsigned int i = 0; i < pbody[0]; i++) {
        oprof[oprof[0] / 4] = i ? (i > 1 ? 0x58595a20 : 0x64657363) : 0x74657874;
        pbody[i * 3 + 2] = oprof[0];
        oprof[0] += (pbody[i * 3 + 3] + 3) & -4;
    }

    memcpy (oprof + 32, pbody, sizeof(pbody));

    // wtpt
    memcpy ((char *)oprof + pbody[8] + 8, pwhite, sizeof(pwhite));

    // r/g/b TRC
    for (int i = 4; i < 7; i++) {
        memcpy ((char *)oprof + pbody[i * 3 + 2], pcurve, sizeof(pcurve));
    }

    // r/g/b XYZ
//    pseudoinverse ((double (*)[3]) out_rgb[output_color-1], inverse, 3);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            oprof[pbody[j * 3 + 23] / 4 + i + 2] = matrix[i][j] * 0x10000 + 0.5;
//      for (num = k=0; k < 3; k++)
//        num += xyzd50_srgb[i][k] * inverse[j][k];
        }

    // convert to network byte order
    for (unsigned int i = 0; i < phead[0] / 4; i++) {
        oprof[i] = htonl(oprof[i]);
    }

    // cprt
    strcpy ((char *)oprof + pbody[2] + 8, "--rawtherapee profile--");

    // desc
    oprof[pbody[5] / 4 + 2] = name.size() + 1;
    strcpy ((char *)oprof + pbody[5] + 12, name.c_str());


    cmsHPROFILE p = cmsOpenProfileFromMem (oprof, ntohl(oprof[0]));
    delete [] oprof;
    return p;
}

}
