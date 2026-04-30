"""
Lip landmark training script for AICamRobot Phase 2b.

Dataset: WFLW (Wider Face Alignment in the Wild)
  Download from: http://mmlab.ie.cuhk.edu.hk/projects/WFLW.html
  Required files: WFLW_annotations.tar.gz, WFLW_images.tar.gz

Usage:
  # Train and export ONNX
  python train_lip_model.py --wflw_root /data/WFLW --output lip_landmark_6pt.onnx --epochs 60

  # Export calibration data for INT8 quantization
  python train_lip_model.py --mode calib --wflw_root /data/WFLW --output calib_faces.npy

Output: lip_landmark_6pt.onnx  (~1.4 MB FP32, ~350 KB INT8 after esp-dl quantization)
"""

import argparse
import os
import numpy as np
from pathlib import Path

import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
from PIL import Image
import torchvision.transforms.functional as TF


# ──────────────────────────────────────────────
# WFLW landmark indices for 6 lip points
# ──────────────────────────────────────────────
# WFLW has 98 landmarks (0-indexed).
# Outer lip contour runs CCW: 76(L corner)→79(top center)→82(R corner)→85(bottom center)
# Inner lip contour: 88(top-L)→90(top center)→91(top-R)→92→94(bottom center)→95→88
#   Left outer corner:   76
#   Right outer corner:  82
#   Top outer center:    79  (philtrum dip / Cupid's bow peak)
#   Bottom outer center: 85  (chin-lip junction)
#   Top inner center:    90  (inner upper lip center)
#   Bottom inner center: 94  (inner lower lip center)
WFLW_LIP_IDX = [76, 82, 79, 85, 90, 94]  # 6 points → 12 output values


# ──────────────────────────────────────────────
# Model: tiny CNN for 6-point lip regression
# ──────────────────────────────────────────────
class LipLandmarkNet(nn.Module):
    """
    Input:  (N, 1, 48, 48)  grayscale, normalized to [-1, 1]
    Output: (N, 12)          6 lip points as (x, y) pairs, normalized to [0, 1]
    """
    def __init__(self):
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 16, 3, padding=1), nn.BatchNorm2d(16), nn.ReLU(inplace=True),
            nn.MaxPool2d(2),               # 24×24×16
            nn.Conv2d(16, 32, 3, padding=1), nn.BatchNorm2d(32), nn.ReLU(inplace=True),
            nn.MaxPool2d(2),               # 12×12×32
            nn.Conv2d(32, 64, 3, padding=1), nn.BatchNorm2d(64), nn.ReLU(inplace=True),
            nn.MaxPool2d(2),               # 6×6×64
        )
        self.head = nn.Sequential(
            nn.Flatten(),
            nn.Linear(6 * 6 * 64, 128), nn.ReLU(inplace=True),
            nn.Dropout(0.3),
            nn.Linear(128, 12),
            nn.Sigmoid(),                  # output in [0, 1]
        )

    def forward(self, x):
        return self.head(self.features(x))


# ──────────────────────────────────────────────
# Dataset
# ──────────────────────────────────────────────
class WFLWLipDataset(Dataset):
    """
    Reads WFLW annotations, crops face to tight bounding box + 20% margin,
    resizes to 48×48 grayscale, extracts 6 lip landmarks normalized to [0,1].
    """

    def __init__(self, root: str, split: str = 'train', augment: bool = True):
        self.root = Path(root)
        self.augment = augment

        ann_dir = self.root / 'WFLW_annotations' / 'list_98pt_rect_attr_train_test'
        if split == 'train':
            ann_file = ann_dir / 'list_98pt_rect_attr_train.txt'
        else:
            ann_file = ann_dir / 'list_98pt_rect_attr_test.txt'

        self.samples = []
        with open(ann_file) as f:
            for line in f:
                parts = line.strip().split()
                # 196 landmark coords, then 4 bbox coords, then attrs, then filename
                coords = list(map(float, parts[:196]))
                bbox = list(map(float, parts[196:200]))
                img_path = self.root / 'WFLW_images' / parts[-1]
                landmarks_98 = np.array(coords).reshape(98, 2)
                self.samples.append((img_path, landmarks_98, bbox))

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        img_path, lm98, bbox = self.samples[idx]
        img = Image.open(img_path).convert('L')  # grayscale

        # Crop with 20% margin around bbox
        x1, y1, x2, y2 = bbox
        w, h = x2 - x1, y2 - y1
        margin_x, margin_y = w * 0.20, h * 0.20
        cx0 = max(0, x1 - margin_x)
        cy0 = max(0, y1 - margin_y)
        cx1 = min(img.width,  x2 + margin_x)
        cy1 = min(img.height, y2 + margin_y)
        crop_w, crop_h = cx1 - cx0, cy1 - cy0

        img_crop = img.crop((cx0, cy0, cx1, cy1))

        # Extract 6 lip points, map to crop coords, normalize to [0, 1]
        pts = lm98[WFLW_LIP_IDX]  # (6, 2)
        pts_crop = pts - np.array([cx0, cy0])
        pts_norm = pts_crop / np.array([crop_w, crop_h])
        pts_norm = np.clip(pts_norm, 0.0, 1.0)

        # Augmentation: horizontal flip
        if self.augment and np.random.rand() < 0.5:
            img_crop = TF.hflip(img_crop)
            pts_norm[:, 0] = 1.0 - pts_norm[:, 0]
            # Swap left/right pairs: pt0 ↔ pt1
            pts_norm[[0, 1]] = pts_norm[[1, 0]]

        # Augmentation: brightness / contrast jitter
        if self.augment:
            img_crop = TF.adjust_brightness(img_crop, 0.7 + np.random.rand() * 0.6)
            img_crop = TF.adjust_contrast(img_crop,  0.7 + np.random.rand() * 0.6)

        img_tensor = TF.to_tensor(img_crop)        # [1, H, W], [0,1]
        img_tensor = TF.resize(img_tensor, [48, 48])
        img_tensor = img_tensor * 2.0 - 1.0        # normalize to [-1, 1]

        target = torch.tensor(pts_norm.flatten(), dtype=torch.float32)
        return img_tensor, target


# ──────────────────────────────────────────────
# NME metric
# ──────────────────────────────────────────────
def compute_nme(pred: torch.Tensor, gt: torch.Tensor) -> float:
    """Normalized Mean Error, normalized by left-right outer corner distance."""
    pred_np = pred.detach().cpu().numpy().reshape(-1, 6, 2)
    gt_np   = gt.detach().cpu().numpy().reshape(-1, 6, 2)
    # inter-outer-corner distance (pt0 and pt1)
    norm_dist = np.linalg.norm(gt_np[:, 0] - gt_np[:, 1], axis=1)
    norm_dist = np.maximum(norm_dist, 1e-6)
    errs = np.linalg.norm(pred_np - gt_np, axis=2).mean(axis=1)
    return float((errs / norm_dist).mean() * 100)  # percentage


# ──────────────────────────────────────────────
# Training loop
# ──────────────────────────────────────────────
def train(args):
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f'Device: {device}')

    train_ds = WFLWLipDataset(args.wflw_root, 'train', augment=True)
    val_ds   = WFLWLipDataset(args.wflw_root, 'test',  augment=False)
    train_loader = DataLoader(train_ds, batch_size=128, shuffle=True, num_workers=4)
    val_loader   = DataLoader(val_ds,   batch_size=256, shuffle=False, num_workers=4)

    model = LipLandmarkNet().to(device)
    optimizer = optim.Adam(model.parameters(), lr=1e-3, weight_decay=1e-4)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)
    criterion = nn.MSELoss()

    best_nme = float('inf')
    for epoch in range(1, args.epochs + 1):
        model.train()
        for imgs, targets in train_loader:
            imgs, targets = imgs.to(device), targets.to(device)
            optimizer.zero_grad()
            loss = criterion(model(imgs), targets)
            loss.backward()
            optimizer.step()
        scheduler.step()

        model.eval()
        all_pred, all_gt = [], []
        with torch.no_grad():
            for imgs, targets in val_loader:
                imgs = imgs.to(device)
                preds = model(imgs)
                all_pred.append(preds.cpu())
                all_gt.append(targets)
        nme = compute_nme(torch.cat(all_pred), torch.cat(all_gt))
        print(f'Epoch {epoch:3d}/{args.epochs}  NME: {nme:.2f}%  LR: {scheduler.get_last_lr()[0]:.2e}')
        if nme < best_nme:
            best_nme = nme
            torch.save(model.state_dict(), '/tmp/lip_best.pt')

    print(f'\nBest NME: {best_nme:.2f}%')
    model.load_state_dict(torch.load('/tmp/lip_best.pt'))

    # Export ONNX
    dummy = torch.zeros(1, 1, 48, 48)
    torch.onnx.export(
        model, dummy, args.output,
        input_names=['input'], output_names=['landmarks'],
        dynamic_axes={'input': {0: 'batch'}, 'landmarks': {0: 'batch'}},
        opset_version=11,
    )
    print(f'Saved ONNX → {args.output}')


# ──────────────────────────────────────────────
# Calibration data export
# ──────────────────────────────────────────────
def export_calib(args):
    ds = WFLWLipDataset(args.wflw_root, 'test', augment=False)
    loader = DataLoader(ds, batch_size=1, shuffle=True)
    crops = []
    for imgs, _ in loader:
        crops.append(imgs.numpy()[0])  # (1, 48, 48)
        if len(crops) >= 200:
            break
    calib = np.stack(crops)
    np.save(args.output, calib)
    print(f'Saved calibration data ({len(crops)} samples) → {args.output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['train', 'calib'], default='train')
    parser.add_argument('--wflw_root', required=True)
    parser.add_argument('--output',    required=True)
    parser.add_argument('--epochs',    type=int, default=60)
    args = parser.parse_args()

    if args.mode == 'train':
        train(args)
    else:
        export_calib(args)
