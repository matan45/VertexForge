// AudioSource - Typed wrapper over the entity's audio source (2D or 3D).
// Stateless view forwarding 1:1 to the Audio static facade.

import * from "../Audio.mt";
import * from "../Entity.mt";
import * from "../ComponentType.mt";
import * from "Component.mt";

public class AudioSource extends Component {
    public constructor(int entityId) : super(entityId) {
    }

    @Override
    public function exists(): bool {
        return Entity::hasComponent(this.entityId, ComponentType::AUDIO_2D) ||
               Entity::hasComponent(this.entityId, ComponentType::AUDIO_3D);
    }

    // --- playback ---

    public function play2d(): int {
        return Audio::play2d(this.entityId);
    }

    public function play3d(): int {
        return Audio::play3d(this.entityId);
    }

    public function stop(): void {
        Audio::stop(this.entityId);
    }

    public function pause(): void {
        Audio::pause(this.entityId);
    }

    public function resume(): void {
        Audio::resume(this.entityId);
    }

    public function isPlaying(): bool {
        return Audio::isPlaying(this.entityId);
    }

    // --- properties ---

    public function volume(): float {
        return Audio::getVolume(this.entityId);
    }

    public function setVolume(float volume): void {
        Audio::setVolume(this.entityId, volume);
    }

    public function pitch(): float {
        return Audio::getPitch(this.entityId);
    }

    public function setPitch(float pitch): void {
        Audio::setPitch(this.entityId, pitch);
    }

    public function loop(): bool {
        return Audio::getLoop(this.entityId);
    }

    public function setLoop(bool loop): void {
        Audio::setLoop(this.entityId, loop);
    }

    // --- routing ---

    public function setBus(string busName): void {
        Audio::setBus(this.entityId, busName);
    }

    public function bus(): string {
        return Audio::getBus(this.entityId);
    }
}
