// AnimatorComponent - Typed wrapper over the entity's animator state machine.
// Stateless view forwarding 1:1 to the Animator static facade. Named
// AnimatorComponent because the static facade already owns the Animator name.

import * from "../Animator.mt";
import * from "Component.mt";

public class AnimatorComponent extends Component {
    public constructor(int entityId) : super(entityId) {
    }

    @Override
    public function exists(): bool {
        return Animator::hasAnimator(this.entityId);
    }

    // --- state ---

    public function currentState(): string {
        return Animator::getCurrentState(this.entityId);
    }

    public function isPlaying(): bool {
        return Animator::isPlaying(this.entityId);
    }

    public function isBlending(): bool {
        return Animator::isBlending(this.entityId);
    }

    public function normalizedTime(): float {
        return Animator::getNormalizedTime(this.entityId);
    }

    // --- parameters ---

    public function setFloat(string paramName, float value): void {
        Animator::setFloat(this.entityId, paramName, value);
    }

    public function setInt(string paramName, int value): void {
        Animator::setInt(this.entityId, paramName, value);
    }

    public function setBool(string paramName, bool value): void {
        Animator::setBool(this.entityId, paramName, value);
    }

    public function setTrigger(string paramName): void {
        Animator::setTrigger(this.entityId, paramName);
    }

    public function getFloat(string paramName): float {
        return Animator::getFloat(this.entityId, paramName);
    }

    public function getInt(string paramName): int {
        return Animator::getInt(this.entityId, paramName);
    }

    public function getBool(string paramName): bool {
        return Animator::getBool(this.entityId, paramName);
    }

    // --- playback ---

    public function play(): void {
        Animator::play(this.entityId);
    }

    public function pause(): void {
        Animator::pause(this.entityId);
    }

    public function stop(): void {
        Animator::stop(this.entityId);
    }

    public function reset(): void {
        Animator::reset(this.entityId);
    }

    public function forceTransitionTo(string stateName, float blendDuration): bool {
        return Animator::forceTransitionTo(this.entityId, stateName, blendDuration);
    }

    public function forceTransitionToImmediate(string stateName): bool {
        return Animator::forceTransitionToImmediate(this.entityId, stateName);
    }

    // --- root motion ---

    public function setRootMotion(bool enabled): void {
        Animator::setRootMotion(this.entityId, enabled);
    }

    public function rootMotion(): bool {
        return Animator::getRootMotion(this.entityId);
    }
}
