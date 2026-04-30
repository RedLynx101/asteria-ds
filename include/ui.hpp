#pragma once

#include <string>
#include <vector>

#include <citro2d.h>
#include <citro3d.h>
#include <tex3ds.h>

#include "models.hpp"

namespace asteria {

struct Colors {
    u32 canvas    = C2D_Color32(235, 238, 244, 255);
    u32 panel     = C2D_Color32(255, 255, 255, 255);
    u32 panelAlt  = C2D_Color32(242, 244, 248, 255);
    u32 stroke    = C2D_Color32(188, 194, 208, 255);
    u32 text      = C2D_Color32(18, 22, 36, 255);
    u32 textSoft  = C2D_Color32(88, 96, 112, 255);
    u32 accent    = C2D_Color32(14, 132, 122, 255);
    u32 accentSoft = C2D_Color32(216, 242, 238, 255);
    u32 mint      = C2D_Color32(224, 243, 230, 255);
    u32 danger    = C2D_Color32(192, 50, 60, 255);
    u32 dangerSoft = C2D_Color32(252, 226, 228, 255);
    u32 success   = C2D_Color32(20, 138, 76, 255);
};

class UiRenderer {
  public:
    bool init();
    void shutdown();
    void beginFrame();
    void endFrame();
    void draw(const AppState& state);
    const std::string& lastError() const { return lastError_; }

    std::vector<ButtonRect> bottomButtons() const { return bottomButtons_; }
    std::vector<ButtonRect> topButtons() const { return topButtons_; }

  private:
    void drawTopChat(const AppState& state);
    void drawTopPilot(const AppState& state);
    void drawBottomNav(const AppState& state);
    void drawBottomChat(const AppState& state);
    void drawBottomPilot(const AppState& state);
    void drawButton(const ButtonRect& button, bool active, bool danger = false);
    void drawCard(float x, float y, float w, float h, u32 fill);
    void drawText(float x, float y, float scale, u32 color, const char* fmt, ...);
    bool syncPreviewTexture(const ImagePreview& preview);
    void clearPreviewTexture();

    Colors colors_;
    C3D_RenderTarget* topTarget_ = nullptr;
    C3D_RenderTarget* bottomTarget_ = nullptr;
    C2D_TextBuf textBuf_ = nullptr;
    bool initialized_ = false;
    std::string lastError_;
    const AppState* frameState_ = nullptr;
    C3D_Tex previewTexture_ {};
    Tex3DS_SubTexture previewSubTexture_ {};
    C2D_Image previewImage_ {};
    bool previewTextureReady_ = false;
    std::string previewTextureSourceKey_;
    mutable std::vector<ButtonRect> bottomButtons_;
    mutable std::vector<ButtonRect> topButtons_;
};

}  // namespace asteria
