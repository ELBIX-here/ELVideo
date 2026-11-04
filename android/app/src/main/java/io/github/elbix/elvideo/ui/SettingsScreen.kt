package io.github.elbix.elvideo.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.elbix.elvideo.BuildConfig
import io.github.elbix.elvideo.app
import io.github.elbix.elvideo.data.Drive

@Composable
fun SettingsScreen(wide: Boolean) {
    val context = LocalContext.current
    val s = context.app.settings
    val driveConnected by s.driveConnected.collectAsState()
    val appearance by s.appearance.collectAsState()
    val speed by s.speed.collectAsState()
    val resume by s.resume.collectAsState()
    val autoNext by s.autoNext.collectAsState()
    val seekStep by s.seekStep.collectAsState()
    val folders by s.folders.collectAsState()
    val c = Elv.colors

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp)
            .then(if (wide) Modifier.widthIn(max = 640.dp) else Modifier),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        PageTitle("설정", Modifier.padding(top = 8.dp, bottom = 10.dp))

        SectionLabel("화면", Modifier.padding(start = 4.dp))
        Card(Modifier.fillMaxWidth()) {
            SettingRow("모드") {
                SegmentedControl(listOf("시스템" to "system", "라이트" to "light", "다크" to "dark"), appearance, s::setAppearance)
            }
        }

        SectionLabel("재생", Modifier.padding(start = 4.dp, top = 12.dp))
        Card(Modifier.fillMaxWidth()) {
            SettingRow("기본 속도") {
                SegmentedControl(listOf("1×" to 1f, "1.25×" to 1.25f, "1.5×" to 1.5f, "2×" to 2f), speed, s::setSpeed)
            }
            Hairline(inset = 16.dp)
            SettingRow("멈춘 곳부터 이어 보기") { ToggleSwitch(resume, s::setResume) }
            Hairline(inset = 16.dp)
            SettingRow("다음 강의 자동 재생", "폴더의 다음 동영상으로 넘어갑니다") { ToggleSwitch(autoNext, s::setAutoNext) }
            Hairline(inset = 16.dp)
            SettingRow("두 번 탭 이동") {
                SegmentedControl(listOf("5초" to 5, "10초" to 10, "30초" to 30), seekStep, s::setSeekStep)
            }
        }

        SectionLabel("Google Drive", Modifier.padding(start = 4.dp, top = 12.dp))
        Card(Modifier.fillMaxWidth()) {
            val connect = LocalConnectDrive.current
            if (driveConnected) {
                SettingRow("연결됨", "Drive 폴더를 읽기 전용으로 엽니다") {
                    PillButton("연결 해제", {
                        Drive.signOut(context)
                        s.setDriveConnected(false)
                    }, style = ButtonStyle.Plain, height = 32.dp)
                }
            } else {
                SettingRow("연결 안 됨", "폴더 → Google Drive 폴더 추가에서도 연결됩니다") {
                    if (connect != null) PillButton("연결", { connect {} }, height = 32.dp)
                }
            }
        }

        if (folders.isNotEmpty()) {
            SectionLabel("폴더", Modifier.padding(start = 4.dp, top = 12.dp))
            Card(Modifier.fillMaxWidth()) {
                folders.forEachIndexed { i, f ->
                    if (i > 0) Hairline(inset = 16.dp)
                    SettingRow(f.name) {
                        PillButton("빼기", { s.removeFolder(f.treeUri) }, style = ButtonStyle.Plain, height = 32.dp)
                    }
                }
            }
        }

        SectionLabel("정보", Modifier.padding(start = 4.dp, top = 12.dp))
        Card(Modifier.fillMaxWidth()) {
            SettingRow("버전") { Text(BuildConfig.VERSION_NAME, style = Type.body, color = c.textDim) }
            Hairline(inset = 16.dp)
            SettingRow("글꼴", "Pretendard · SIL Open Font License") {}
            Hairline(inset = 16.dp)
            SettingRow("재생 엔진", "AndroidX Media3 (ExoPlayer)") {}
        }
    }
}
