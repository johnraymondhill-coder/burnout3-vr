package io.github.xenophobentx.takedawn;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.Map;

/** A normal Quest window for choosing graphics before the immersive app starts. */
public final class QuestSettingsActivity extends Activity {
    private static final String[] SCALES = {"1.5", "3", "3.5"};
    private static final String[] LABELS = {
        "Performance — 960 × 720 per eye",
        "Balanced — 1920 × 1440 per eye",
        "Sharper — 2240 × 1680 per eye"};

    static void apply(Context context, Map<String,String> env) {
        SharedPreferences preferences=context.getSharedPreferences("quest-vr", MODE_PRIVATE);
        int preset = preferences.getInt("quality", 1);
        preset = Math.max(0, Math.min(SCALES.length - 1, preset));
        env.putIfAbsent("RECOMP_RENDER_SCALE", SCALES[preset]);
        env.putIfAbsent("BO3_QUEST_STEREO", "1");
        env.putIfAbsent("RECOMP_INPUT_CONTROL_SCHEME", "xbox");
        env.putIfAbsent("RECOMP_GFX_MOTION_BLUR", "0");
        env.putIfAbsent("RECOMP_GFX_HIGH_REFRESH", "0");
        env.putIfAbsent("RECOMP_GFX_HDR", "0");
        env.putIfAbsent("RECOMP_PRESENT_SPIN_LEAD", "0");
        env.putIfAbsent("RECOMP_GFX_AA", preferences.getBoolean("smooth",true) ? (preset==0?"fxaa":"smaa") : "off");
        env.putIfAbsent("RECOMP_GFX_SHARPEN","0.12");
        env.putIfAbsent("RECOMP_GFX_ANISO",preset==0?"2":"8");
        env.putIfAbsent("BO3_QUEST_HUD_SCALE",preferences.getBoolean("compact_hud",true)?"0.62":"1");
        env.putIfAbsent("BO3_QUEST_FOVEATION",preferences.getBoolean("foveation",true)?"1":"0");
    }
    private int dp(int value) { return (int)(value * getResources().getDisplayMetrics().density + .5f); }
    private TextView text(String value, int size) {
        TextView view = new TextView(this);
        view.setText(value); view.setTextColor(Color.WHITE); view.setTextSize(size);
        view.setPadding(0, dp(8), 0, dp(8)); return view;
    }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        SharedPreferences settings = getSharedPreferences("quest-vr", MODE_PRIVATE);
        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL); body.setGravity(Gravity.CENTER_HORIZONTAL);
        body.setPadding(dp(28),dp(20),dp(28),dp(20)); body.setBackgroundColor(Color.rgb(20,24,30));
        body.addView(text("Burnout 3 VR graphics", 27));
        body.addView(text("Balanced is the starting point. Choose Performance if races slow down. Sharper costs more GPU time. Changes take effect on the next launch.", 17));
        RadioGroup choices = new RadioGroup(this);
        for(int i=0;i<LABELS.length;i++) {
            RadioButton button = new RadioButton(this);
            button.setId(100+i); button.setText(LABELS[i]); button.setTextColor(Color.WHITE);
            button.setTextSize(20); button.setPadding(dp(8),dp(8),dp(8),dp(8));
            choices.addView(button);
        }
        choices.check(100 + Math.max(0,Math.min(2,settings.getInt("quality",1))));
        body.addView(choices);
        CheckBox smooth=new CheckBox(this);smooth.setText("Smooth edges (anti-aliasing)");smooth.setTextColor(Color.WHITE);smooth.setTextSize(18);
        smooth.setChecked(settings.getBoolean("smooth",true));body.addView(smooth);
        CheckBox compact=new CheckBox(this);compact.setText("Compact HUD — keep the edges in view");compact.setTextColor(Color.WHITE);compact.setTextSize(18);
        compact.setChecked(settings.getBoolean("compact_hud",true));body.addView(compact);
        CheckBox foveation=new CheckBox(this);foveation.setText("Fixed foveation — sharp center, lower-detail outer view");foveation.setTextColor(Color.WHITE);foveation.setTextSize(18);
        foveation.setChecked(settings.getBoolean("foveation",true));body.addView(foveation);
        Button launch = new Button(this); launch.setText("Save & launch VR"); launch.setTextSize(20);
        launch.setOnClickListener(view -> {
            settings.edit().putInt("quality",choices.getCheckedRadioButtonId()-100)
                .putBoolean("smooth",smooth.isChecked()).putBoolean("compact_hud",compact.isChecked())
                .putBoolean("foveation",foveation.isChecked()).commit();
            if(!GameActivity.restartRunning())startActivity(new Intent(this, QuestStartActivity.class));
            finish();
        });
        body.addView(launch,new LinearLayout.LayoutParams(-1,dp(60)));
        body.addView(text("Tap left Menu to pause. Hold it briefly to recenter, or click the right thumbstick. A confirms; B returns. Racing enters VR at GO. Use the left trigger to brake; tap it while steering to drift.",16));
        ScrollView scroll = new ScrollView(this); scroll.addView(body); setContentView(scroll);
    }
}
