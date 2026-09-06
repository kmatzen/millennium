from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAYER = ROOT / "host/os_image/layer/millennium-phone-image.yaml"


def test_wifi_state_owner_is_resolved_inside_target_root() -> None:
    text = LAYER.read_text()

    create = 'install -d -m 0700 "$1/var/lib/millennium/wifi"'
    target_chown = (
        'chroot "$1" chown -R millennium:millennium '
        "/var/lib/millennium /var/log/millennium"
    )

    assert 'install -d -o millennium -g millennium' not in text
    assert create in text
    assert target_chown in text
    assert text.index(create) < text.index(target_chown)


def test_fixed_shared_generator_is_installed_after_upstream_customize() -> None:
    text = LAYER.read_text()

    cleanup = "  cleanup-hooks:"
    source = '"$SRCROOT/slot-shared-generator"'
    destination = (
        '"$1/usr/lib/systemd/system-generators/slot-shared-generator"'
    )

    assert cleanup in text
    assert text.index(cleanup) > text.index("  customize-hooks:")
    cleanup_text = text[text.index(cleanup):]
    assert "install -D -m 0755" in cleanup_text
    assert source in cleanup_text
    assert destination in cleanup_text
    assert "cmp " in cleanup_text
