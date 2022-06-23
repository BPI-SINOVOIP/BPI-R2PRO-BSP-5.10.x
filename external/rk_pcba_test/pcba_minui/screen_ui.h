/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RECOVERY_SCREEN_UI_H
#define RECOVERY_SCREEN_UI_H

#include <pthread.h>

#include "ui.h"
#include "../../bootable/recovery/minui/minui.h"

#ifdef RK3288_PCBA
#define CHAR_WIDTH  36
#define CHAR_HEIGHT 50
#else
#define CHAR_WIDTH  10
#define CHAR_HEIGHT 18
#endif

// Implementation of RecoveryUI appropriate for devices with a screen
// (shows an icon + a progress bar, text logging, menu, etc.)
class ScreenRecoveryUI : public RecoveryUI {
  public:
    ScreenRecoveryUI();

    void Init();

    // overall recovery state ("background image")
    void SetBackground(Icon icon);

    // progress indicator
    void SetProgressType(ProgressType type);
    void ShowProgress(float portion, float seconds);
    void SetProgress(float fraction);

    // text log
    void ShowText(bool visible);
    bool IsTextVisible();
    bool WasTextEverVisible();

    // printing messages
    void Print(const char* fmt, ...); // __attribute__((format(printf, 1, 2)));
    void Print(int t_col,int t_row,int r,int g,int b,int a, const char* fmt,...);
	void ChangeMenuItem(int index,int t_col,int t_row,int r,int g,int b,int a, const char* fmt,...);

	void FillColor(int r,int g,int b,int a,int left,int top,int width,int height);

    // menu display
    void StartMenu(const char* const * headers, const char* const * items,
                           int initial_selection);
    void StartMenu(const char* const * headers, const char* const * items,
                           int initial_selection,int left_col,int top_row);
    int SelectMenu(int sel);
    void EndMenu();

  private:
    Icon currentIcon;
    int installingFrame;

    pthread_mutex_t updateMutex;
    gr_surface backgroundIcon[3];
    gr_surface *installationOverlay;
    gr_surface *progressBarIndeterminate;
    gr_surface progressBarEmpty;
    gr_surface progressBarFill;

    ProgressType progressBarType;

    float progressScopeStart, progressScopeSize, progress;
    double progressScopeTime, progressScopeDuration;

    // true when both graphics pages are the same (except for the
    // progress bar)
    bool pagesIdentical;

    static const int kMaxCols = 96;
    static const int kMaxRows = 32;
	static const int kMaxTiles = 50;

	static const int DEFAULT_R = 64;
	static const int DEFAULT_G = 96;
	static const int DEFAULT_B = 255;
	static const int DEFAULT_A = 255;

	static const int SELECT_MENU_R = 255;
	static const int SELECT_MENU_G = 255;
	static const int SELECT_MENU_B = 255;
	static const int SELECT_MENU_A = 255;

    // Log text overlay, displayed when a magic key is pressed
    char text[kMaxRows][kMaxCols];
	typedef struct{
		int t_col,t_row,r,g,b,a;
    } textInfo;
	textInfo itemsInfo[kMaxRows];
    typedef struct{
		int left,top,right,bottom,r,g,b,a;
    } FillColorTile;
	FillColorTile  tiles[kMaxTiles];
	int tiles_count;
    int text_cols, text_rows;
    int text_col, text_row, text_top;
    bool show_text;
    bool show_text_ever;   // has show_text ever been true?

    char menu[kMaxRows][kMaxCols];
	textInfo menuItemsInfo[kMaxRows];
    bool show_menu;
    int menu_top, menu_items, menu_sel;

    pthread_t progress_t;

    int animation_fps;
    int indeterminate_frames;
    int installing_frames;
    int install_overlay_offset_x, install_overlay_offset_y;

    void draw_install_overlay_locked(int frame);
    void draw_background_locked(Icon icon);
    void draw_progress_locked();
    void draw_text_line(int left,int top, const char* t);
    void draw_screen_locked();
    void update_screen_locked();
    void update_progress_locked();
    static void* progress_thread(void* cookie);
    void progress_loop();

    void LoadBitmap(const char* filename, gr_surface* surface);

};

#endif  // RECOVERY_UI_H
