<?php
header('Content-Type: image/png');
header('Cache-Control: public, max-age=604800');

$s  = max(64, min(512, (int)($_GET['size'] ?? 192)));
$img = imagecreatetruecolor($s, $s);
imageantialias($img, true);

$bg    = imagecolorallocate($img, 26,  26,  26);   // #1a1a1a
$green = imagecolorallocate($img, 44,  160, 44);   // #2ca02c
$white = imagecolorallocate($img, 230, 230, 230);
$dark  = imagecolorallocate($img, 20,  20,  20);

imagefill($img, 0, 0, $bg);

// Thermometer proportions
$cx = (int)($s / 2);
$tw = max(5, (int)($s * 0.12));   // half tube width
$ty = (int)($s * 0.13);           // tube top y
$th = (int)($s * 0.46);           // tube height
$br = (int)($s * 0.17);           // bulb radius
$by = $ty + $th + (int)($br * 0.55);

// --- Outer tube (green) ---
imagefilledellipse($img, $cx, $ty + $tw, $tw * 2, $tw * 2, $green);          // rounded top
imagefilledrectangle($img, $cx - $tw, $ty + $tw, $cx + $tw, $ty + $th, $green);

// --- Inner tube (dark = empty upper half) ---
$iw = max(2, (int)($tw * 0.52));
imagefilledellipse($img, $cx, $ty + $iw + 2, $iw * 2, $iw * 2, $dark);
imagefilledrectangle($img, $cx - $iw, $ty + $iw + 2, $cx + $iw, $ty + (int)($th * 0.48), $dark);

// --- Bulb (green circle) ---
imagefilledellipse($img, $cx, $by, ($br + 3) * 2, ($br + 3) * 2, $green);

// --- Degree symbol (small circle top-right of tube) ---
$dr = max(3, (int)($s * 0.055));
$dx = $cx + $tw + $dr + (int)($s * 0.04);
$dy = $ty + (int)($s * 0.08);
imagefilledellipse($img, $dx, $dy, $dr * 2, $dr * 2, $white);
imagefilledellipse($img, $dx, $dy, (int)($dr * 1.0), (int)($dr * 1.0), $bg);

imagepng($img);
imagedestroy($img);
?>
