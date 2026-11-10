package io.github.xenophobentx.takedawn;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import java.io.File;

/** Quest uses the runtime's Vulkan device, not Android's advertised feature level. */
public final class QuestStartActivity extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        File files = GameActivity.filesDir(this);
        new File(files, GameActivity.GAME_DIR).mkdirs();
        startActivity(new Intent(this, StartActivity.dataComplete(files)
                ? GameActivity.class : SetupActivity.class));
        finish();
    }
}
