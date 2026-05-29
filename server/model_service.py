from __future__ import annotations

import dataclasses
import os
import urllib.request
from io import BytesIO
from pathlib import Path


WEIGHTS_URL = (
    "https://raw.githubusercontent.com/sarayangrh/Cross-Species-Translation/main/"
    "DogBarkTranslator/Models_format_ipynb/weightsForContextPredict.pth"
)
CONTEXT_CLASSES = ["Play", "Aggression", "Neutral"]
ANGRY_THRESHOLD = 0.55


@dataclasses.dataclass(frozen=True)
class Prediction:
    label: str
    source_class: str
    confidence: float
    model_error: str | None = None


class CNNPredictor:
    def __init__(self, weights_path: str | os.PathLike[str] | None = None, auto_download: bool = True):
        self.weights_path = Path(weights_path or os.environ.get("DOG_TRANSLATOR_WEIGHTS", "server/models/weightsForContextPredict.pth"))
        self.auto_download = auto_download
        self._model = None
        self._torch = None
        self._torchaudio = None
        self._mel_transform = None

    def predict(self, wav_bytes: bytes) -> Prediction:
        try:
            self._ensure_loaded()
            return self._predict_loaded(wav_bytes)
        except Exception as exc:
            return Prediction(label="non_angry", source_class="error", confidence=0.0, model_error=str(exc))

    def _ensure_loaded(self) -> None:
        if self._model is not None:
            return

        if not self.weights_path.exists():
            if not self.auto_download:
                raise FileNotFoundError(f"model weights not found: {self.weights_path}")
            self.weights_path.parent.mkdir(parents=True, exist_ok=True)
            urllib.request.urlretrieve(WEIGHTS_URL, self.weights_path)

        import torch
        import torch.nn as nn
        import torch.nn.functional as F
        import torchaudio

        class CNN(nn.Module):
            def __init__(self, input_channels: int = 1, num_classes: int = 3):
                super().__init__()
                self.conv1 = nn.Conv2d(input_channels, 16, kernel_size=3, padding="same")
                self.bn1 = nn.BatchNorm2d(16)
                self.conv2 = nn.Conv2d(16, 32, kernel_size=3, padding="same")
                self.bn2 = nn.BatchNorm2d(32)
                self.conv3 = nn.Conv2d(32, 64, kernel_size=3, padding="same")
                self.bn3 = nn.BatchNorm2d(64)
                self.conv4 = nn.Conv2d(64, 128, kernel_size=3, padding="same")
                self.bn4 = nn.BatchNorm2d(128)
                self.dropout = nn.Dropout(p=0.5)
                self.flattened_size = self._flattened_size((1, 1, 59, 344))
                self.linear = nn.Linear(self.flattened_size, num_classes)

            def _flattened_size(self, input_shape):
                with torch.no_grad():
                    x = torch.zeros(input_shape)
                    for conv, bn in ((self.conv1, self.bn1), (self.conv2, self.bn2), (self.conv3, self.bn3), (self.conv4, self.bn4)):
                        x = F.max_pool2d(F.relu(bn(conv(x))), 2)
                    return x.numel()

            def forward(self, x):
                for conv, bn in ((self.conv1, self.bn1), (self.conv2, self.bn2), (self.conv3, self.bn3), (self.conv4, self.bn4)):
                    x = F.max_pool2d(F.relu(bn(conv(x))), 2)
                x = self.dropout(x)
                x = torch.flatten(x, start_dim=1)
                return F.softmax(self.linear(x), dim=1)

        model = CNN(input_channels=1, num_classes=3)
        state_dict = torch.load(self.weights_path, weights_only=False, map_location=torch.device("cpu"))
        model.load_state_dict(state_dict)
        model.eval()

        self._torch = torch
        self._torchaudio = torchaudio
        self._mel_transform = torchaudio.transforms.MelSpectrogram(sample_rate=16000, n_fft=1024, hop_length=256, n_mels=59)
        self._model = model

    def _predict_loaded(self, wav_bytes: bytes) -> Prediction:
        torch = self._torch
        torchaudio = self._torchaudio
        waveform, sample_rate = torchaudio.load(BytesIO(wav_bytes))

        if sample_rate != 16000:
            waveform = torchaudio.transforms.Resample(orig_freq=sample_rate, new_freq=16000)(waveform)
        if waveform.shape[0] > 1:
            waveform = waveform.mean(dim=0, keepdim=True)

        mel_spec = self._mel_transform(waveform)
        if mel_spec.shape[2] < 344:
            import torch.nn.functional as F

            mel_spec = F.pad(mel_spec, (0, 344 - mel_spec.shape[2]))
        else:
            mel_spec = mel_spec[:, :, :344]
        mel_spec = mel_spec.unsqueeze(0)

        with torch.no_grad():
            probabilities = self._model(mel_spec)[0]
            confidence, index = torch.max(probabilities, dim=0)

        source_class = CONTEXT_CLASSES[int(index.item())]
        score = float(confidence.item())
        label = "angry" if source_class == "Aggression" and score >= ANGRY_THRESHOLD else "non_angry"
        return Prediction(label=label, source_class=source_class, confidence=score)
